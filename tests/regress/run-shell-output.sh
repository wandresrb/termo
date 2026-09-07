#!/bin/sh

# 4476
# run-shell should go to stdout if present without -t

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

TMP=$(mktemp)
trap "$TERMO kill-server 2>/dev/null; rm -f $TMP" 0 1 15

$TERMO -f/dev/null new -d "$TERMO run 'echo foo' >$TMP; sleep 10" || exit 1
sleep 1 && [ "$(cat $TMP)" = "foo" ] || exit 1

$TERMO -f/dev/null new -d "$TERMO run -t: 'echo foo' >$TMP; sleep 10" || exit 1
sleep 1 && [ "$(cat $TMP)" = "" ] || exit 1
[ "$($TERMO display -p '#{pane_mode}')" = "view-mode" ] || exit 1

$TERMO -f/dev/null new -d -s t1 'sleep 10' || exit 1
$TERMO -f/dev/null run -d 1 'echo delayed' >$TMP 2>&1 &
pid=$!
sleep 0.2
kill -9 "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
sleep 2
$TERMO has-session -t t1 || exit 1

$TERMO kill-server 2>/dev/null

exit 0
