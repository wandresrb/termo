#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"
$TERMO2 kill-server 2>/dev/null

TMP=$(mktemp)
trap "rm -f $TMP" 0 1 15

$TERMO2 -f/dev/null new -d || exit 1
$TERMO2 set -as terminal-overrides ',*:am@' || exit 1
$TERMO2 set -g status-right 'RRR' || exit 1
$TERMO2 set -g status-left 'LLL' || exit 1
$TERMO2 set -g window-status-current-format 'WWW' || exit 1
$TERMO -f/dev/null new -x20 -y2 -d "$TERMO2 attach" || exit 1
sleep 1
$TERMO capturep -p|tail -1 >$TMP || exit 1
$TERMO kill-server 2>/dev/null
$TERMO2 kill-server 2>/dev/null
cat <<EOF|cmp -s $TMP - || exit 1
LLLWWW           RR
EOF

exit 0
