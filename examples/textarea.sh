#!/bin/bash


rm ./toytui.state

t="../build/release/toytui"


main=`$t box new id:main w:80 h:25 border_bgcol:17 border_fgcol:25 border:1 scrollbarY:on title:"LOREM IPSUM"`
ta=`$t textarea new id:ta parent:$main bg:black fg:iwhite tabsize:4 wrap:full align:justify src:"lorem2.txt"`

$t core set setfocus:$main

ret=`$t core get keyboard keys_exit:"[ESC][ENTER]q"`

echo; echo
echo "output:"
echo $ret
$t core set mode:destroy
