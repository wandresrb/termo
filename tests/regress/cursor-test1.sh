#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

TMP=$(mktemp)
trap "rm -f $TMP" 0 1 15

$TERMO -f/dev/null new -d -x40 -y10 \
      "cat cursor-test.txt; printf '\e[9;15H'; cat" || exit 1
$TERMO set -g window-size manual || exit 1

$TERMO display -pF '#{cursor_x} #{cursor_y} #{cursor_character}' >>$TMP
$TERMO capturep -p|awk '{print NR-1,$0}' >>$TMP
$TERMO resizew -x10 || exit 1
$TERMO display -pF '#{cursor_x} #{cursor_y} #{cursor_character}' >>$TMP
$TERMO capturep -p|awk '{print NR-1,$0}' >>$TMP
$TERMO resizew -x50 || exit 1
$TERMO display -pF '#{cursor_x} #{cursor_y} #{cursor_character}' >>$TMP
$TERMO capturep -p|awk '{print NR-1,$0}' >>$TMP

cmp -s $TMP cursor-test1.result || exit 1

$TERMO kill-server 2>/dev/null
exit 0
