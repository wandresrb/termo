#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
TMP=$(mktemp)
trap "rm -f $TMP" 0 1 15
$TERMO kill-server 2>/dev/null

$TERMO -f/dev/null \
	  set -g remain-on-exit on \; \
	  set -g remain-on-exit-format '' \; \
      new -d -- cat UTF-8-test.txt
sleep 1
$TERMO capturep -pCeJS- >$TMP
$TERMO kill-server

cmp -s $TMP utf8-test.result || exit 1
exit 0
