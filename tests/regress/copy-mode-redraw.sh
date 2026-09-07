#!/bin/sh

# Exercise copy mode redraw through a real client. An inner termo is attached
# inside an outer termo pane; the outer pane is captured to inspect what the
# inner client actually drew.

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

fail() {
	echo "$*" >&2
	exit 1
}

capture() {
	$TERMO capturep -pS0 -E- >$TMP || exit 1
}

check_line() {
	line=$1
	want=$2
	got=$(sed -n "$line"p $TMP)
	[ "$got" = "$want" ] || fail "line $line: expected '$want', got '$got'"
}

check_grep() {
	pattern=$1
	grep -Fq "$pattern" $TMP || fail "missing pattern: $pattern"
}

check_no_grep() {
	pattern=$1
	grep -Fq "$pattern" $TMP && fail "unexpected pattern: $pattern"
}

redraw() {
	$TERMO2 send -X cursor-right || exit 1
	sleep 1
	capture
}

$TERMO kill-server 2>/dev/null
$TERMO2 kill-server 2>/dev/null

TMP=$(mktemp)
BEFORE=$(mktemp)
AFTER=$(mktemp)
trap "rm -f $TMP $BEFORE $AFTER; $TERMO kill-server 2>/dev/null; $TERMO2 kill-server 2>/dev/null" 0 1 15

$TERMO2 -f/dev/null new -d -x48 -y8 -s test \
	"awk 'BEGIN { for (i = 0; i < 90; i++) { if (i % 2 == 0) printf \"L%03d-ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-END\\n\", i; else printf \"S%03d\\n\", i } }'; exec sleep 100" || exit 1
$TERMO2 set -g status off || exit 1
$TERMO2 set -g mode-keys vi || exit 1
$TERMO2 set -g copy-mode-line-numbers off || exit 1
$TERMO2 set -g copy-mode-position-format "#[align=right][23/100-LONGTAIL]" || exit 1

$TERMO -f/dev/null new -d -x48 -y8 || exit 1
$TERMO set -g status off || exit 1
$TERMO send -l "$TERMO2 attach" || exit 1
$TERMO send Enter || exit 1
sleep 1

CLIENT=$($TERMO2 list-clients -F '#{client_name}' | head -1)
[ -n "$CLIENT" ] || fail "no inner client"

$TERMO2 copy-mode || exit 1
$TERMO2 send -X history-top || exit 1
sleep 1

# Shrinking and moving the position indicator should not leave stale text from
# the previous indicator, for different widths and alignments.
for size in 20 48; do
	$TERMO resizew -x$size -y8 || exit 1
	$TERMO2 refresh -t"$CLIENT" -c || exit 1
	sleep 1
	for align in left centre right; do
		$TERMO2 set -g copy-mode-position-format "#[align=$align][23/100-LONGTAIL]" || exit 1
		redraw
		check_grep "[23/100-LONGTAIL]"

		$TERMO2 set -g copy-mode-position-format "#[align=$align][1/100]" || exit 1
		redraw
		check_grep "[1/100]"
		check_no_grep "LONGTAIL"
		check_no_grep "[1/100]]"
	done

	$TERMO2 set -g copy-mode-position-format "#[align=right][1/100]" || exit 1
	redraw
	$TERMO2 set -g copy-mode-position-format "#[align=right][23/100]" || exit 1
	redraw
	check_grep "[23/100]"
done

# Scrolling from long pane content to short pane content should clear the
# remainder of the old line without relying on a full pre-clear.
$TERMO resizew -x48 -y8 || exit 1
$TERMO2 refresh -t"$CLIENT" -c || exit 1
$TERMO2 send -X cancel || exit 1
$TERMO2 copy-mode -H || exit 1
$TERMO2 send -X history-top || exit 1
sleep 1
capture
check_line 1 "L000-ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-END"

$TERMO2 send -X scroll-down || exit 1
sleep 1
capture
check_line 1 "S001"

$TERMO2 send -X scroll-down || exit 1
sleep 1
capture
check_line 1 "L002-ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-END"

$TERMO2 send -X scroll-down || exit 1
sleep 1
capture
check_line 1 "S003"

# Scrolling from long content to a tabbed short line should still clear the old
# line tail.
$TERMO2 send -X cancel || exit 1
$TERMO resizew -x40 -y8 || exit 1
$TERMO2 refresh -t"$CLIENT" -c || exit 1
$TERMO2 new-window \
	"printf 'LONGTAIL-ABCDEFGHIJKLMNOPQRSTUV\nA\tB\nLONGTAIL-123456789012345678\nS\n'; i=0; while [ \$i -lt 20 ]; do printf 'FILLER-%02d\n' \$i; i=\$((i + 1)); done; exec sleep 100" || \
	exit 1
sleep 1
$TERMO2 copy-mode -H || exit 1
$TERMO2 send -X history-top || exit 1
sleep 1
capture
check_line 1 "LONGTAIL-ABCDEFGHIJKLMNOPQRSTUV"

$TERMO2 send -X scroll-down || exit 1
sleep 1
capture
check_line 1 "A       B"
sed -n 1p $TMP | grep -Fq "LONGTAIL" && \
	fail "short tabbed line left stale tail"

# Reflow can leave padding from tabs at the start of wrapped lines. Entering
# copy mode should redraw those padding cells as spaces, not skip them and move
# the following text left.
$TERMO2 send -X cancel || exit 1
$TERMO resizew -x80 -y24 || exit 1
$TERMO2 refresh -t"$CLIENT" -c || exit 1
$TERMO2 new-window "cat ../tmux.c; exec sleep 100" || exit 1
sleep 1
$TERMO2 splitw -hd || exit 1
sleep 1
$TERMO capturep -pS0 -E- >$BEFORE || exit 1
$TERMO2 copy-mode -H -t:.0 || exit 1
sleep 1
$TERMO capturep -pS0 -E- >$AFTER || exit 1

if ! cmp -s "$BEFORE" "$AFTER"; then
	diff -u "$BEFORE" "$AFTER" >&2
	fail "copy-mode redraw moved reflowed tab padding"
fi

exit 0
