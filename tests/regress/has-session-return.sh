#!/bin/sh

# 971
# has-session should return 1 on error

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

$TERMO -f/dev/null has -tfoo </dev/null 2>/dev/null && exit 1
$TERMO -f/dev/null start\; has -tfoo </dev/null 2>/dev/null && exit 1
$TERMO -f/dev/null new -d\; has -tfoo </dev/null 2>/dev/null && exit 1
$TERMO -f/dev/null new -dsfoo\; has -tfoo </dev/null 2>/dev/null || exit 1
$TERMO kill-server 2>/dev/null

exit 0
