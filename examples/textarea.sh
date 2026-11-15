#!/bin/bash

clear

t="../build/release/toytui"
rm toytui.state
dummy=`$t core set mode destroy` 
dummy=`$t core set term_cursor:off`

main=`$t box new id:main w:80 h:25 x:3 y:1 border_bgcol:17 border_fgcol:25 border:1 scrollbarY:on title:"LOREM IPSUM"`
ta=`$t textarea new id:ta parent:$main bg:black fg:iwhite tabsize:4 wrap:full align:justify src:"lorem.txt"`
info=`$t box new id:info z_index:99 x:40 w:32 h:4 border:1 `
infota=`$t textarea new id:infota parent:$info wrap:full text:"scroll:[UP]/[DOWN]\nExit:[ESC] or [ENTER] or 'q'"`

$t core set setfocus:$main

ret=`$t core get keyboard keys_exit:"[ESC][ENTER]q"`

echo; echo
echo "output:"
echo $ret
$t core set term_cursor:on
$t core set mode:destroy
