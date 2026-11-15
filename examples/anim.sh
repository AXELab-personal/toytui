#!/bin/bash

clear

t="../build/release/toytui"
rm toytui.state
dummy=`$t core set mode destroy` 
dummy=`$t core set term_cursor:off`
$t core set mode:daemon

termw=0
termh=0
bgcol=17

racketH=5
racketY=1
racketX=0	

gateH=22
gateY=0
gateX=0
		
ballCC=15
ballX=2
ballY=2
ballDX=1; ballDY=1

counter=0
counterC="iyellow"
counterB="auto"
updateCounter=false;

countermap=(
	1 1 1 1 0 1 1 0 1 1 0 1 1 1 1 
	0 1 0 0 1 0 0 1 0 0 1 0 0 1 0 
	1 1 1 0 0 1 1 1 1 1 0 0 1 1 1 
	1 1 1 0 0 1 1 1 1 0 0 1 1 1 1 
	1 0 1 1 0 1 1 1 1 0 0 1 0 0 1 
	1 1 1 1 0 0 1 1 1 0 0 1 1 1 1 
	1 1 1 1 0 0 1 1 1 1 0 1 1 1 1 
	1 1 1 0 0 1 0 0 1 0 0 1 0 0 1 
	1 1 1 1 0 1 1 1 1 1 0 1 1 1 1 
	1 1 1 1 0 1 1 1 1 0 0 1 1 1 1
	)

bd=1
ballC=10
ballEnabled=true
collisions=0
colg=(
76 77 78 79 80 81 82 83 84 85 86 87
)

exec 3</dev/tty
stty -icanon -echo min 0 time 0 < /dev/tty
key=''
xset r rate 150 15

keypoll() {
    local c c2 c3

    [[ -z $key ]] || return 0

    IFS= read -r -s -N 1 -t 0.002 -u 3 c || return 0

    case $c in
        $'\r'|$'\n')
            key="enter"
            ;;

        $'\e')
            IFS= read -r -s -N 1 -t 0.002 -u 3 c2 || return 0
            IFS= read -r -s -N 1 -t 0.002 -u 3 c3 || return 0

            case "$c$c2$c3" in
                $'\e[A'|$'\eOA') key="up" ;;
                $'\e[B'|$'\eOB') key="dn" ;;
            esac
            ;;

        q|Q)
            key="q"
            ;;
    esac
}

setTermSize() {
	termw=`$t core get width | cut -f2 -d':'`
	termh=`$t core get height | cut -f2 -d':'`
}

setBallColor() {

	if [[ $ballC -gt $((${#colg[@]}-2)) ]] || [[ $ballC -lt 1 ]]; then bd=$(($bd * -1)); fi
	ballC=$((ballC+bd)); 
	ballCC=${colg[$ballC]}
	((collisions++))
}

showCounter() {
	ch=$((counter/10))
	cl=$((counter - ( $ch*10) ))
	lbase=$((cl * 15));
	hbase=$((ch * 15));
	
	for q in $(seq 0 4); do
		for w in $(seq 0 2); do

			lpos=$(($lbase+$w+($q*3) ))
			hpos=$(($hbase+$w+($q*3) ))
			lbg="auto"
			hbg="auto"
			if [ ${countermap[$lpos]} == "1" ]; then lbg=$counterC; fi
			if [ ${countermap[$hpos]} == "1" ]; then hbg=$counterC; fi
			
			$t label set id:seg0$q$w parent:$ct bg:$lbg
			$t label set id:seg1$q$w parent:$ct bg:$hbg
		done
	done
}

moveRacket() {
	case $key in
		$'dn')  
				if [[ $racketY -lt $((termh - $racketH -2)) ]]; then ((racketY++)); fi
				;;
		$'up')  
				if [[ $racketY -gt 0 ]]; then ((racketY--)); fi
				;;
	esac 
	$t box set id:racket h:$racketH y:$racketY
	key="";
}

moveBall() {
	
	if $ballEnabled ; then
#ball move	
		if [[ $ballX -lt 1  ]] || [[ $ballX -gt $(($termw - 5)) ]]; then ballDX=$(($ballDX * -1)); setBallColor; fi
		if [[ $ballY -gt $(($termh - 4)) ]] || [[ $ballY -lt 1  ]]; then ballDY=$(($ballDY * -1)); setBallColor; fi

		ballX=$(($ballX+$ballDX))
		ballY=$(($ballY+$ballDY))
		$t label set id:$ball x:$ballX y:$ballY fg:$ballCC
#rakcet collision	
		if [[ $ballX -eq $((racketX-2)) ]]; then
			if [[ $ballY -gt $((racketY -1 )) ]] && [[ $ballY -lt $((racketY + racketH)) ]]; then 
				ballDX=$(($ballDX * -1)); setBallColor; 

			fi
		fi

#gate collision
		if [[ $ballX -eq $(($gateX-3)) ]]; then
			if [[ $ballY -gt $((gateY -2)) ]] && [[ $ballY -lt $((gateY+gateH-1)) ]]; then
				ballEnabled=false;
			fi
		fi
		
		
		if (! $ballEnabled); then 
			$t label set id:$ball fg:160 attrib:blink
			((counter++))
			updateCounter=true
			fi
		
	fi	

}

init() {

setTermSize


# playfield elements 
main=`$t box new id:main border:1 bg:$bgcol title:"..:: TUIPONG ::.." titlePos:right`

info=`$t box new id:info border:1 bg:73 fg:black w:23 h:5 x:3 y:2 z_index:99`
intota=`$t textarea new id:infota parent:$info text:"Rakcet: [UP]/[DOWN]\nRestart:[ENTER]\nExit:[Q]"`
gateY=$(( (termh /2)-(gateH /2 ) ))
gateX=$((termw - 1))

gate=`$t box new id:gate bg:52 h:$gateH w:1 y:$gateY x:$gateX`

front=`$t box new id:front parent:$main bg:0 h:$gateH w:3 y:$((gateY-1)) x:$((gateX-4)) z-index:1`

racketX=$(($termw-7))
racket=`$t box new id:racket parent:$main bg:igreen x:$racketX w:1 h:$racketH y:$racketY z_index:3`

ball=`$t label new id:ball w:2 parent:$main fg:$ballC x:1 y:1 z_index:2 text:"██"`
ct=` $t box new id:ct parent:$main bg:$counterB w:14 h:5 x:$(($(($termw / 2))-7 ))`

# counter
for q in $(seq 0 4); do
	for w in $(seq 0 2); do
		declare seg1$q$w=`$t label new id:seg1$q$w parent:$ct w:2 x:$(($w*2)) y:$q`
		declare seg1$q$w=`$t label new id:seg0$q$w parent:$ct w:2 x:$((8 + (($w*2)) )) y:$q`
	done
done
	
}

# START
init
showCounter

while true; do
	keypoll
	case $key in
		'q')  
				xset r rate 500 20
				$t core set term_cursor:on
				$t core set mode:destroy
				
				reset
				exit 0
				;;
		'enter')
				if ( ! $ballEnabled ); then
					ballX=`shuf -i 1-10 -n 1`
					ballY=`shuf -i 1-10 -n 1`
					$t label set id:$ball fg:160 attrib:auto
					ballEnabled=true
					collisions=0
				fi
				;;

	esac

	moveBall
	moveRacket

	if $updateCounter; then
		updateCounter=false
		showCounter
		fi

	sleep 0.01
	done
	
	