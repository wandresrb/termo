#!/bin/sh

# Exercise drawing of popups (display-popup) over the window scene. A popup is an
# overlay drawn on top of the redraw scene (the overlay_draw path in
# screen-redraw.c), so this guards against regressions in how popups appear.
#
# A popup is modal and stays open until its command exits, so each scene fully
# re-creates the servers and re-attaches; the popup is opened in the background
# (display-popup blocks the client that runs it) and the outer pane is captured
# while it is open.
#
# Run with GENERATE=1 to (re)create the golden files.

PATH=/bin:/usr/bin
TERM=screen
LC_ALL=C.UTF-8
export TERM LC_ALL

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO=
TMUX=
TERMO2=
TMUX2=
SETUP=0
RESULTS=screen-redraw-results

TMP=$(mktemp)

cleanup() {
	rm -f "$TMP"
	[ -n "$TERMO" ] && $TERMO kill-server 2>/dev/null
	[ -n "$TERMO2" ] && $TERMO2 kill-server 2>/dev/null
}
trap cleanup 0 1 15

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

C="sh -c 'i=0; while [ \$i -lt 13 ]; do printf \"POP%02d abcdefghij\n\" \$i; i=\$((i + 1)); done; exec sleep 100'"

# setup: fresh inner window attached inside a fresh outer pane, 40x14.
setup() {
	[ -n "$TERMO" ] && $TERMO kill-server 2>/dev/null
	[ -n "$TERMO2" ] && $TERMO2 kill-server 2>/dev/null
	SETUP=$((SETUP + 1))
	TERMO="$TEST_TERMO -LtestA$$-$SETUP -f/dev/null"
	TMUX="$TERMO"
	TERMO2="$TEST_TERMO -LtestB$$-$SETUP -f/dev/null"
	TMUX2="$TERMO2"
	$TERMO2 new -d -x40 -y14 "$C" || exit 1
	$TERMO2 set -g status off || exit 1
	$TERMO2 set -g window-size manual || exit 1
	$TERMO2 resizew -x40 -y14 || exit 1
	$TERMO new -d -x40 -y14 || exit 1
	$TERMO set -g status off || exit 1
	$TERMO set -g window-size manual || exit 1
	$TERMO set -g default-terminal "tmux-256color" || exit 1
	$TERMO send -l "$TERMO2 attach" || exit 1
	$TERMO send Enter || exit 1
	sleep 1
}

# popup <args>: open a popup running a fixed command, in the background (it stays
# open because the command sleeps; the servers are killed at the next setup).
popup() {
	$TERMO2 display-popup "$@" -E "sh -c 'printf POPUP; exec sleep 100'" &
	sleep 1
}

# Basic popup over a single pane.
setup
popup -w20 -h6 -x6 -y3
compare popup-basic

# Popup over a split: drawn on top of the pane border.
setup
$TERMO2 splitw -h "$C" || exit 1
popup -w24 -h8 -x8 -y3
compare popup-over-split

# Popup with no border lines (-B).
setup
popup -B -w20 -h6 -x6 -y3
compare popup-noborder

# Popup with double border lines.
setup
popup -b double -w20 -h6 -x6 -y3
compare popup-double

exit 0
