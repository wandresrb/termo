#!/bin/sh

# Exercise how the client status line affects the window scene drawn by
# screen-redraw.c. The status line is not part of the scene, but its size and
# position change the scene's offset and height (status_line_size and the
# REDRAW_STATUS_TOP flag in screen-redraw.c). The status line content does not
# matter, so status-format is set empty; only the offset effect is tested.
#
# A top/bottom split makes the offset visible: the horizontal border and the
# pane contents move as the status line grows or changes side.
#
# Each scene is rendered in an inner termo attached inside an outer termo pane.
# The outer pane is captured and compared with a golden in screen-redraw-results/.
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
	$TERMO capturep -p >$TMP || exit 1
	if [ -n "$GENERATE" ]; then
		cp $TMP "$RESULTS/$1.result" || exit 1
		echo "generated $1"
	else
		cmp -s $TMP "$RESULTS/$1.result" || \
			fail "scene $1 differs from $RESULTS/$1.result"
	fi
}

$TERMO kill-server 2>/dev/null
$TERMO2 kill-server 2>/dev/null

# Inner: a top/bottom split. The window tracks the attached client size, so the
# status line shrinks/shifts the window scene. Status content is blanked.
$TERMO2 new -d -x30 -y12 "sh -c 'printf TOP; exec sleep 100'" || exit 1
$TERMO2 set -g window-size latest || exit 1
i=0
while [ $i -le 4 ]; do
	$TERMO2 set -g status-format[$i] "" || exit 1
	i=$((i + 1))
done
$TERMO2 splitw -v "sh -c 'printf BOT; exec sleep 100'" || exit 1

$TERMO new -d -x30 -y12 || exit 1
$TERMO set -g status off || exit 1
$TERMO set -g window-size manual || exit 1
$TERMO set -g default-terminal "tmux-256color" || exit 1
$TERMO send -l "$TERMO2 attach" || exit 1
$TERMO send Enter || exit 1
sleep 1

# No status line: the window fills the whole client.
$TERMO2 set -g status off || exit 1
compare status-off

# One line at the bottom: the scene loses its bottom row.
$TERMO2 set -g status on || exit 1
$TERMO2 set -g status-position bottom || exit 1
compare status-bottom

# One line at the top: the whole scene is shifted down one row.
$TERMO2 set -g status-position top || exit 1
compare status-top

# Three lines at the bottom.
$TERMO2 set -g status 3 || exit 1
$TERMO2 set -g status-position bottom || exit 1
compare status-3-bottom

# Three lines at the top: the scene is shifted down three rows.
$TERMO2 set -g status-position top || exit 1
compare status-3-top

exit 0
