#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$-1 -f/dev/null"
TMUX="$TERMO"

TMP=$(mktemp)
trap 'rm -f "$TMP"; $TERMO kill-server 2>/dev/null' 0 1 15

cat <<EOF >$TMP
new -sfoo -nfoo0; neww -nfoo1; neww -nfoo2
new -sbar -nbar0; neww -nbar1; neww -nbar2
EOF
$TERMO -f$TMP start </dev/null || exit 1
sleep 1
$TERMO lsw -aF '#{session_name},#{window_name}'|sort >$TMP || exit 1
$TERMO kill-server 2>/dev/null
cat <<EOF|cmp -s $TMP - || exit 1
bar,bar0
bar,bar1
bar,bar2
foo,foo0
foo,foo1
foo,foo2
EOF

TERMO="$TEST_TERMO -LtestA$$-2 -f/dev/null"

TMUX="$TERMO"
cat <<EOF >$TMP
new -sfoo -nfoo0
neww -nfoo1
neww -nfoo2
new -sbar -nbar0
neww -nbar1
neww -nbar2
EOF
$TERMO -f$TMP start </dev/null || exit 1
sleep 1
$TERMO lsw -aF '#{session_name},#{window_name}'|sort >$TMP || exit 1
$TERMO kill-server 2>/dev/null
cat <<EOF|cmp -s $TMP - || exit 1
bar,bar0
bar,bar1
bar,bar2
foo,foo0
foo,foo1
foo,foo2
EOF

exit 0
