#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

TMP=$(mktemp)
trap "rm -f $TMP" 0 1 15

$TERMO -f/dev/null new -d -x200 -y200 || exit 1
$TERMO -f/dev/null splitw || exit 1
sleep 1
cat <<EOF|$TERMO -C a >$TMP
refresh-client -C 200x200
selectp -t%0
splitw
neww
splitw
selectp -t%0
killp -t%1
swapp -t%2 -s%3
neww
splitw
splitw
selectl tiled
killw
EOF
sleep 1
$TERMO has || exit 1
$TERMO lsp -aF '#{pane_id} #{window_layout}' >$TMP || exit 1
cat <<EOF|cmp -s $TMP - || exit 1
%0 f5ab,200x200,0,0[200x50,0,0,0,200x149,0,51,3]
%3 f5ab,200x200,0,0[200x50,0,0,0,200x149,0,51,3]
%2 dcbd,200x200,0,0[200x100,0,0,2,200x99,0,101,4]
%4 dcbd,200x200,0,0[200x100,0,0,2,200x99,0,101,4]
EOF
$TERMO kill-server 2>/dev/null

exit 0
