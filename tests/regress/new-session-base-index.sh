#!/bin/sh

# new session base-index

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
set -g base-index 100
new
set base-index 200
neww
EOF

$TERMO -f$TMP start
echo $($TERMO lsw -F'#{window_index}') >$TMP
(echo "100 200"|cmp -s - $TMP) || exit 1
$TERMO kill-server 2>/dev/null

exit 0
