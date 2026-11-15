#!/bin/bash

clear

t="../build/release/toytui"
rm toytui.state
dummy=`$t core set mode destroy` 
dummy=`$t core set term_cursor:off`


imp=$($t input new id:inp y:2 keys_accept:[ENTER] keys_exit:[TAB] \
		 pattern:'name:\e[48;5;25m{%s0-10}\e[0m MAC:{%X2}:\e[1;33m{%X2}\e[0m:{%X2}:{%X2}:{%X2}:{%X2}')

$t core set setfocus:input@inp
$t input set id:inp cursorType:bU 
ret=`$t core get keyboard`

echo; echo
echo "output:"
echo $ret
$t core set term_cursor:on
$t core set mode:destroy
