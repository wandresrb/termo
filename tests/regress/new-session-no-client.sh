#!/bin/sh

# 869
# new with no client (that is, from the config file) should imply -d and
# not attach

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
new -stest
EOF

$TERMO -f$TMP start || exit 1
sleep 1 && $TERMO has -t=test: || exit 1
$TERMO kill-server 2>/dev/null

exit 0
