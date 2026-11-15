#!/bin/bash

clear

t="../build/release/toytui"

rm toytui.state
dummy=`$t core set mode destroy` 
dummy=`$t core set term_cursor:off`
mainbox=`$t box new id:"mainbox" parent:core@terminal bg:4 fg:11 border:2 title:"\e[3;4;30;106mMAIN\e[0m BOX"`
parent=`$t box new id:"parent_box" parent:$mainbox w:60 h:28 x:2 bg:26 fg:iwhite border:1  title:"PARENT" sby:1 sbx:1 scrollbarX:on scrollbarY:on z-index:2`
child=`$t box new id:"child_box" parent:$parent w:32 h:16 border:0 title:"Child" bg:4 z_index:2`


cw=`$t box get id:$child width|cut -f2 -d':'`;
ch=`$t box get id:$child height|cut -f2 -d':'`;
pw=`$t box get id:$parent width|cut -f2 -d':'`;
ph=`$t box get id:$parent height|cut -f2 -d':'`;

mx=$(($pw - $cw))
my=$(($ph - $ch))

for y in $(seq 0 15); do
	for x in $(seq 0 15); do
	x=`$t box new id:"dummy_"$(($(($y*16))+$x)) parent:$child opacity:off w:2 h:1 bg:$(($(($y*16))+$x)) x:$(($x * 2)) y:$y`
	done
done	


infobox=`$t box new id:info w:30 h:3 border:1 bg:48 fg:black z_index:99 x:18 y:8`
infota=`$t textarea new id:infotext parent:$infobox align:center text:"Press any key to continue"`


dummy=`$t core get keypress`

$t box set id:$infobox bg:75 y:11
$t textarea set id:$infota text:"Press [ESC] to exit"

x=0; y=0;
dx=1; dy=1;

key="[NUL]"

while [ "$key" != "[ESC]" ]; do
	
	a=`$t box set id:$parent offsetX:$x offsetY:$y`
	x=$(($x+$dx));
	y=$(($y+$dy));
	
	if [[ $x -gt $(($mx-3)) ]]; then
		dx=$(($dx * -1));	

	fi

	if [[ $x -lt 1 ]]; then
		dx=$(($dx * -1));

	fi

	if [[ $y -gt $(($my-3)) ]]; then
		dy=$(($dy * -1));
	fi

	if [[ $y -lt 1 ]]; then
		dy=$(($dy * -1));
	fi

    sleep 0.05

key=`$t core get keypress noblock:on`

done

$t core set term_cursor:on
echo
$t core set mode:destroy

