#!/bin/sh

# new-session without clients should be the right size

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$-1 -f/dev/null"
TMUX="$TERMO"

TMP=$(mktemp)
trap 'rm -f "$TMP"; $TERMO kill-server 2>/dev/null' 0 1 15

$TERMO -f/dev/null new -d </dev/null || exit 1
sleep 1
$TERMO ls -F "#{window_width} #{window_height}" >$TMP
printf "80 24\n"|cmp -s $TMP - || exit 1
$TERMO kill-server 2>/dev/null

TERMO="$TEST_TERMO -LtestA$$-2 -f/dev/null"

TMUX="$TERMO"
$TERMO -f/dev/null new -d -x 100 -y 50 </dev/null || exit 1
sleep 1
$TERMO ls -F "#{window_width} #{window_height}" >$TMP
printf "100 50\n"|cmp -s $TMP - || exit 1
$TERMO kill-server 2>/dev/null

exit 0
