#!/bin/sh

# 884
# capture-pane should send colours after SGR 0

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

TMP=$(mktemp)
trap "rm -f $TMP" 0 1 15

$TERMO -f/dev/null new -d "
	printf '\033[31;42;1mabc\033[0;31mdef\n'
	printf '\033[m\033[100m bright bg \033[m'
	$TERMO capturep -peS0 -E1 >>$TMP"


sleep 1

(
	printf '\033[1m\033[31m\033[42mabc\033[0m\033[31mdef\033[39m\n'
	printf '\033[100m bright bg \033[49m\n'
) | cmp - $TMP || exit 1

$TERMO has 2>/dev/null && exit 1

exit 0
