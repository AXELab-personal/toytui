#!/bin/bash

clear

t="../build/release/toytui"

rm toytui.state
dummy=`$t core set mode destroy` 

termW=`$t core get width|cut -f2 -d':'`
termH=`$t core get height|cut -f2 -d':'`

cw=5

spcX=$(( (termW-(cw*17))/2 ))
spcY=$(( (termH/2)-(18/2) ))

main=`$t box new id:main border:1 x:$((spcX+2)) y:$((spcY)) w:$(( (cw*16)+2)) h:18 title:"..:: Toytui Color Chart"`


for y in $(seq 0 15); do
	for x in $(seq 0 15); do
	x=`$t label new id:"dummy_"$(($(($y*16))+$x)) parent:$main w:5 h:1 bg:$(($(($y*16))+$x)) x:$(($x * 5)) y:$y text:" "$(( ($y*16) + $x))" "`
	done
done	


echo
echo
$t core set mode:destroy
