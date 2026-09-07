#!/bin/sh

# Test new-pane -M creates a floating pane from a mouse drag.

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"

cleanup()
{
	$TERMO kill-server >/dev/null 2>&1
	$TERMO2 kill-server >/dev/null 2>&1
}
fail()
{
	echo "$*" >&2
	cleanup
	exit 1
}
must_equal()
{
	got=$1
	want=$2
	[ "$got" = "$want" ] || fail "got '$got', expected '$want'"
}

# drag STARTCOL STARTROW ENDCOL ENDROW
#
# Write an SGR Ctrl-mouse press, drag update and release at 1-based positions
# to the outer pane holding the inner client.
drag()
{
	scol="$1"
	srow="$2"
	ecol="$3"
	erow="$4"

	seq=$(printf '\033[<16;%s;%sM' "$scol" "$srow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<48;%s;%sM' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<16;%s;%sm' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}

# right_click COL ROW KEY
#
# Open the right-click menu at a 1-based position and choose KEY.
right_click()
{
	col="$1"
	row="$2"
	key="$3"

	seq=$(printf '\033[<2;%s;%sM' "$col" "$row")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	$TERMO2 send-keys -t "$OUTER" "$key" 2>/dev/null
	sleep 1
}

cleanup

$TERMO new-session -d -s inner -x 80 -y 24 || exit 1
$TERMO set -g mouse on
$TERMO set -g default-command 'sleep 100'
keys=$($TERMO list-keys -T root -F '#{key_command}' MouseDown3Empty) ||
	fail "list MouseDown3Empty failed"
case "$keys" in
*"New Pane"*"New Window"*) ;;
*) fail "missing empty-area menu binding: $keys" ;;
esac

$TERMO2 new-session -d -x 80 -y 24 "$TERMO attach -t inner" || exit 1
sleep 1
OUTER=$($TERMO2 list-panes -F '#{pane_id}' | head -1)
[ -n "$OUTER" ] || fail "No outer pane."
BASE=$($TERMO list-panes -F '#{pane_id}' | head -1)
[ -n "$BASE" ] || fail "No base pane."

# Drag from SGR column 3,row 1 to column 10,row 5. These are window positions
# 2,0 to 9,4. With the default border, the floating pane content is inset by
# one cell and has size 6x3.
drag 3 1 10 5

id=$($TERMO list-panes -F '#{?pane_floating_flag,#{pane_id},}' | tail -n 1)
[ -n "$id" ] || fail "no floating pane created"

must_equal "$($TERMO display-message -p -t "$id" '#{pane_left}')" 3
must_equal "$($TERMO display-message -p -t "$id" '#{pane_top}')" 1
must_equal "$($TERMO display-message -p -t "$id" '#{pane_width}')" 6
must_equal "$($TERMO display-message -p -t "$id" '#{pane_height}')" 3

$TERMO kill-pane -t "$id" || fail "kill floating pane failed"
$TERMO break-pane -W -s "$BASE" || fail "break base pane failed"
$TERMO resize-pane -t "$BASE" -x10 -y4 || fail "resize floating base failed"
$TERMO move-pane -t "$BASE" -P top-left || fail "move floating base failed"

# Right-click in empty window space and choose New Pane. This creates a new
# floating pane and tiles it.
right_click 20 8 p

id=$($TERMO list-panes -F '#{?pane_active,#{pane_id},}' | tail -n 1)
[ -n "$id" ] || fail "no pane created from empty-area menu"
[ "$id" != "$BASE" ] || fail "empty-area menu did not create a new pane"

must_equal "$($TERMO display-message -p -t "$id" '#{pane_floating_flag}')" 0
$TERMO kill-pane -t "$id" || fail "kill empty-area menu pane failed"

# Drag in empty window space with no tiled pane underneath.
drag 40 10 50 15

id=$($TERMO list-panes -F '#{?pane_active,#{pane_id},}' | tail -n 1)
[ -n "$id" ] || fail "no floating pane created from empty area"
[ "$id" != "$BASE" ] || fail "empty-area drag did not create a new pane"

must_equal "$($TERMO display-message -p -t "$id" '#{pane_left}')" 40
must_equal "$($TERMO display-message -p -t "$id" '#{pane_top}')" 10
must_equal "$($TERMO display-message -p -t "$id" '#{pane_width}')" 9
must_equal "$($TERMO display-message -p -t "$id" '#{pane_height}')" 4

TILED=$($TERMO new-pane -PF '#{pane_id}' 'sleep 100') ||
	fail "new pane from only floating panes failed"
$TERMO join-pane -t "$TILED" ||
	fail "tile new pane from only floating panes failed"
must_equal "$($TERMO display-message -p -t "$TILED" '#{pane_floating_flag}')" 0

TILED=$($TERMO new-window -dPF '#{pane_id}' 'sleep 100') ||
	fail "new tiled window failed"
$TERMO new-pane -d -M -L -t "$TILED" 'sleep 100' || fail "new-pane -M -L failed"

cleanup
exit 0
