#!/bin/bash


rm ./toytui.state

t="../build/release/toytui"


imp=$($t input new id:inp y:2 keys_accept:[ENTER] keys_exit:[TAB] \
		 pattern:'name:\e[48;5;25m{%s0-10}\e[0m MAC:{%X2}:\e[1;33m{%X2}\e[0m:{%X2}:{%X2}:{%X2}:{%X2}')

$t core set setfocus:input@inp
$t input set id:inp cursorType:bU 
ret=`$t core get keyboard`

echo; echo
echo "output:"
echo $ret
$t core set mode:destroy
