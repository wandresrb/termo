#!/bin/sh

# Exercise fill-character as a format. The window is smaller than the attached
# client so OUTSIDE spans exist, then the only tiled pane is removed so EMPTY
# spans exist inside the window around a floating pane.

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

TMP=$(mktemp)
trap "rm -f $TMP; $TERMO kill-server 2>/dev/null; $TERMO2 kill-server 2>/dev/null" \
	0 1 15

fail() {
	echo "$*" >&2
	exit 1
}

must_equal() {
	if [ "$1" != "$2" ]; then
		fail "expected '$2', got '$1'"
	fi
}

$TERMO kill-server 2>/dev/null
$TERMO2 kill-server 2>/dev/null

$TERMO2 new -d -x40 -y12 "sh -c 'printf base; exec sleep 100'" || exit 1
$TERMO2 set -g status off || exit 1
$TERMO2 set -g window-size manual || exit 1
$TERMO2 setw fill-character '#{?is_inside,I,#{?is_outside,O,X}}' || exit 1
$TERMO2 resizew -x28 -y8 || exit 1
$TERMO2 new-pane -x12 -y4 -X8 -Y2 "sh -c 'printf FLOAT; exec sleep 100'" || exit 1
tiled=$($TERMO2 list-panes -F '#{pane_floating_flag} #{pane_id}' | \
	awk '$1==0{print $2; exit}') || exit 1
$TERMO2 kill-pane -t "$tiled" || exit 1

$TERMO new -d -x40 -y12 || exit 1
$TERMO set -g status off || exit 1
$TERMO set -g window-size manual || exit 1
$TERMO set -g default-terminal "tmux-256color" || exit 1
$TERMO send -l "$TERMO2 attach" || exit 1
$TERMO send Enter || exit 1
sleep 1

$TERMO capturep -p >$TMP || exit 1

must_equal "$(sed -n '1p' $TMP | cut -c1-28)" \
    "IIIIIIIIIIIIIIIIIIIIIIIIIIII"
must_equal "$(sed -n '1p' $TMP | cut -c29-40)" "OOOOOOOOOOOO"
must_equal "$(sed -n '9p' $TMP)" "OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO"

if grep -q X "$TMP"; then
	fail "fill-character used neither inside nor outside"
fi

exit 0
