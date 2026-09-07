#!/bin/sh

# 947
# size in control mode should change after refresh-client -C, per-window
# control client sizes should work, and -x and -y should work without -d for
# control clients

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$-1 -f/dev/null"
TMUX="$TERMO"

TMP=$(mktemp)
OUT=$(mktemp)
trap 'rm -f "$TMP" "$OUT"; $TERMO kill-server 2>/dev/null' 0 1 15

$TERMO -f/dev/null new -d || exit 1
sleep 1
cat <<EOF|$TERMO -C a >$TMP
ls -F':#{window_width} #{window_height}'
refresh -C 100,50
EOF
grep ^: $TMP >$OUT
$TERMO ls -F':#{window_width} #{window_height}' >>$OUT
printf ":80 24\n:100 50\n"|cmp -s $OUT - || exit 1
$TERMO kill-server 2>/dev/null

TERMO="$TEST_TERMO -LtestA$$-2 -f/dev/null"

TMUX="$TERMO"
$TERMO -f/dev/null new -d || exit 1
sleep 1
cat <<EOF|$TERMO -f/dev/null -C a >$TMP
ls -F':#{window_width} #{window_height}'
refresh -C 80,24
EOF
grep ^: $TMP >$OUT
$TERMO ls -F':#{window_width} #{window_height}' >>$OUT
printf ":80 24\n:80 24\n"|cmp -s $OUT - || exit 1
$TERMO kill-server 2>/dev/null

TERMO="$TEST_TERMO -LtestA$$-3 -f/dev/null"

TMUX="$TERMO"
cat <<EOF|$TERMO -f/dev/null -C new -x 100 -y 50 >$TMP
ls -F':#{window_width} #{window_height}'
refresh -C 80,24
EOF
grep ^: $TMP >$OUT
$TERMO ls -F':#{window_width} #{window_height}' >>$OUT
printf ":100 50\n:80 24\n"|cmp -s $OUT - || exit 1
$TERMO kill-server 2>/dev/null

TERMO="$TEST_TERMO -LtestA$$-4 -f/dev/null"

TMUX="$TERMO"
$TERMO -f/dev/null new -d || exit 1
$TERMO neww || exit 1
w0=$($TERMO display -p -t :0 '#{window_id}')
w1=$($TERMO display -p -t :1 '#{window_id}')
cat <<EOF|$TERMO -f/dev/null -C a >$TMP
refresh -C 80,24
refresh -C ${w0}:40x10
display -p -t ${w0} 'w0 #{window_width} #{window_height}'
display -p -t ${w1} 'w1 #{window_width} #{window_height}'
refresh -C ${w0}:
display -p -t ${w0} 'w0c #{window_width} #{window_height}'
EOF
grep ^w $TMP >$OUT
printf "w0 40 10\nw1 80 24\nw0c 80 24\n"|cmp -s $OUT - || exit 1
$TERMO kill-server 2>/dev/null

exit 0
