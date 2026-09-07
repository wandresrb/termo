#!/bin/sh

# when we kill a session, processes running in it should be killed

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null
sleep 1

$TERMO -f/dev/null new -d 'sleep 1000' || exit 1
P=$($TERMO display -pt0:0.0 '#{pane_pid}')
$TERMO -f/dev/null new -d || exit 1
sleep 1
$TERMO kill-session -t0:
sleep 3
kill -0 $P 2>/dev/null && exit 1
$TERMO kill-server 2>/dev/null

exit 0
