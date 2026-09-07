#!/bin/sh

# new session command

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

TMP=$(mktemp)
trap "rm -f $TMP" 0 1 15

cat <<EOF >$TMP
new sleep 101
new -- sleep 102
new "sleep 103"
EOF

$TERMO -f$TMP start
[ $($TERMO ls|wc -l) -eq 3 ] || exit 1
$TERMO kill-server 2>/dev/null

exit 0
