#!/bin/sh

# Test for GitHub issue #4780 - pane-border-indicators both arrows missing
# on second pane in a two-pane horizontal split.
#
# When pane-border-indicators is set to "both", arrow indicators should
# appear when EITHER pane is selected. Before the fix, arrows only appeared
# when the LEFT pane was selected.

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null
TERMO_OUTER="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX_OUTER="$TERMO_OUTER"
$TERMO_OUTER kill-server 2>/dev/null

trap "$TERMO kill-server 2>/dev/null; $TERMO_OUTER kill-server 2>/dev/null" 0 1 15

# Start outer termo that will capture the inner termo's rendering
$TERMO_OUTER -f/dev/null new -d -x80 -y24 "$TERMO -f/dev/null new -x78 -y22" || exit 1
sleep 1

# Set pane-border-indicators to "both" in inner termo
$TERMO set -g pane-border-indicators both || exit 1

# Create horizontal split (two panes side by side)
$TERMO splitw -h || exit 1
sleep 1

# Helper to check for arrow characters in captured output
has_arrow() {
    echo "$1" | grep -qE '(←|→|↑|↓)'
}

# Test 1: Select left pane (pane 0) and check for arrows
$TERMO selectp -t 0
sleep 1
left_output=$($TERMO_OUTER capturep -Cep 2>/dev/null)
has_arrow "$left_output" || exit 1

# Test 2: Select right pane (pane 1) and check for arrows
# This is the case that failed before the fix
$TERMO selectp -t 1
sleep 1
right_output=$($TERMO_OUTER capturep -Cep 2>/dev/null)
has_arrow "$right_output" || exit 1

$TERMO kill-server 2>/dev/null
$TERMO_OUTER kill-server 2>/dev/null
exit 0
