#!/bin/sh

# Check that mouse selection in an active menu uses the correct coordinates
# when the status line is at the top.

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

# click COL ROW
#
# Write an SGR mouse press then release at a 1-based position to the outer pane
# holding the inner client.
click()
{
	col="$1"
	row="$2"

	seq=$(printf '\033[<0;%s;%sM\033[<0;%s;%sm' \
	    "$col" "$row" "$col" "$row")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}

cleanup

$TERMO new-session -d -s inner -x 80 -y 24 'sleep 100' || exit 1
$TERMO set -g mouse on || exit 1
$TERMO set -g status-position top || exit 1
$TERMO set -g @menu-choice '' || exit 1

$TERMO2 new-session -d -x 80 -y 24 "$TERMO attach -t inner" || exit 1
sleep 1
OUTER=$($TERMO2 list-panes -F '#{pane_id}' | head -1)
[ -n "$OUTER" ] || fail "No outer pane."

$TERMO display-menu -M -x 5 -y 7 \
    "First item" f "set -g @menu-choice first" \
    "Second item" s "set -g @menu-choice second" || exit 1
sleep 1

# -y is the bottom of the menu, so with four menu lines this puts the menu at
# window y=3. The first item is then at window y=4. With one status line at the
# top, this is terminal row 6 in SGR's 1-based coordinates.
click 8 6

choice=$($TERMO show -gv @menu-choice 2>/dev/null)
[ "$choice" = "first" ] || fail "got '$choice', expected 'first'"

cleanup
exit 0
