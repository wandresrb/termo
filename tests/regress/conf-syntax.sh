#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO=
TMUX=
n=0
trap '[ -n "$TERMO" ] && $TERMO kill-server 2>/dev/null' 0 1 15

for i in conf/*.conf; do
	n=$((n + 1))
	TERMO="$TEST_TERMO -LtestA$$-$n -f/dev/null"
	TMUX="$TERMO"
	$TERMO -f/dev/null start \; source -n $i || exit 1
done

exit 0
