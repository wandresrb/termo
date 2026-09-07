#!/bin/sh

# Exercise window-style and window-active-style, which set the default cell
# (background) style for a window's panes. screen-redraw.c uses these for the
# default grid cell of each pane (the active pane uses window-active-style, the
# others window-style). Captured with -e to record the background colours.
#
# Run with GENERATE=1 to (re)create the golden files.

PATH=/bin:/usr/bin
TERM=screen
LC_ALL=C.UTF-8
export TERM LC_ALL

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"
RESULTS=screen-redraw-results

TMP=$(mktemp)
trap "rm -f $TMP; $TERMO kill-server 2>/dev/null; $TERMO2 kill-server 2>/dev/null" \
	0 1 15

fail() {
	echo "$*" >&2
	exit 1
}

compare() {
	sleep 1
	$TERMO capturep -pe >$TMP || exit 1
	if [ -n "$GENERATE" ]; then
		cp $TMP "$RESULTS/$1.result" || exit 1
		echo "generated $1"
	else
		cmp -s $TMP "$RESULTS/$1.result" || \
			fail "scene $1 differs from $RESULTS/$1.result"
	fi
}

new_scene() {
	$TERMO2 neww -d "sh -c 'i=0; while [ \$i -lt 7 ]; do printf \"STYLE%02d abcdefghij\n\" \$i; i=\$((i + 1)); done; exec sleep 100'" || exit 1
	$TERMO2 selectw -t:\$ || exit 1
	$TERMO2 resizew -x40 -y8 || exit 1
}

C="sh -c 'i=0; while [ \$i -lt 7 ]; do printf \"STYLE%02d abcdefghij\n\" \$i; i=\$((i + 1)); done; exec sleep 100'"

$TERMO kill-server 2>/dev/null
$TERMO2 kill-server 2>/dev/null

$TERMO2 new -d -x40 -y8 "sh -c 'i=0; while [ \$i -lt 7 ]; do printf \"STYLE%02d abcdefghij\n\" \$i; i=\$((i + 1)); done; exec sleep 100'" || exit 1
$TERMO2 set -g status off || exit 1
$TERMO2 set -g window-size manual || exit 1

$TERMO new -d -x40 -y8 || exit 1
$TERMO set -g status off || exit 1
$TERMO set -g window-size manual || exit 1
$TERMO set -g default-terminal "tmux-256color" || exit 1
$TERMO send -l "$TERMO2 attach" || exit 1
$TERMO send Enter || exit 1
sleep 1

# Single pane with a window background style.
new_scene
$TERMO2 setw window-style "bg=blue" || exit 1
compare window-style-single

# Split: the active pane uses window-active-style, the other window-style.
new_scene
$TERMO2 setw window-style "bg=blue" || exit 1
$TERMO2 setw window-active-style "bg=red" || exit 1
$TERMO2 splitw -h "$C" || exit 1
$TERMO2 selectp -t0 || exit 1
compare window-style-active

exit 0
