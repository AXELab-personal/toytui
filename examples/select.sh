#!/bin/bash

rm ./toytui.state

t="../build/release/toytui"
$t core set mode:destroy
$t core set term_cursor:off

sel=`$t select new id:sel w:25 h:10 border:1 bg:black fg:iwhite cbg:68 brdfg:22 keys_exit:'[ESC][TAB]' keys_accept:'[ENTER]' title:"LOREM" \
options:'
Lorem ipsum |
dolor sit amet |
consectetur |
adipisicing elit |
sed do eiusmod |
tempor incididunt |
ut labore |
et dolore magna aliqua' 
`

$t core set setfocus:$sel

res=$($t core get keyboard)      # pl. exit:[ENTER] selected:2

echo; echo
$t core set term_cursor:on
$t core set mode:destroy
echo output: $res

