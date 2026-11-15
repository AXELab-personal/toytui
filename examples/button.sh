#!/bin/bash

clear

t="../build/release/toytui"
rm toytui.state
dummy=`$t core set mode destroy` 
dummy=`$t core set term_cursor:off`

$t core set bg:16

btn=`$t button new id:ok x:2 y:8 w:6 h:1 delay:200 text:'[ OK ]'`

$t core set setfocus:button@ok
key=`$t core get keypress`
$t button set id:ok trigger:on
$t core set term_cursor:on

echo; echo
echo output:
echo $key