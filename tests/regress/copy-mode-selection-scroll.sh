#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"
$TERMO kill-server 2>/dev/null
$TERMO2 kill-server 2>/dev/null

cleanup()
{
	$TERMO kill-server 2>/dev/null
	$TERMO2 kill-server 2>/dev/null
}
fail()
{
	echo "$1"
	cleanup
	exit 1
}
expect_buffer()
{
	expected=$1
	actual=$($TERMO show-buffer)
	[ "$actual" = "$expected" ] ||
		fail "unexpected buffer: expected [$expected], got [$actual]"
}
wheel()
{
	button=$1
	col=$2
	row=$3
	seq=$(printf '\033[<%s;%s;%sM' "$button" "$col" "$row")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}
trap cleanup 0
trap 'exit 1' 1 2 3 15

$TERMO new -d -x40 -y10 \
	'i=0; while [ $i -lt 80 ]; do printf "line %02d xxxxxxxxxx\n" $i; i=$((i + 1)); done; cat' ||
	exit 1
$TERMO set -g window-size manual || exit 1
$TERMO set -g mouse on || exit 1

$TERMO copy-mode || exit 1
$TERMO send-keys -X history-top || exit 1
$TERMO send-keys -N10 -X cursor-down || exit 1
$TERMO send-keys -X start-of-line || exit 1
$TERMO send-keys -X begin-selection || exit 1
$TERMO send-keys -N2 -X cursor-down || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1

initial=$(printf 'line 10 xxxxxxxxxx\nline 11 xxxxxxxxxx')
expect_buffer "$initial"

$TERMO send-keys -X stop-selection || exit 1
$TERMO send-keys -N3 -X scroll-down || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$initial"

$TERMO send-keys -N2 -X scroll-up || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$initial"

$TERMO send-keys -X scroll-middle || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$initial"

$TERMO send-keys -X scroll-bottom || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$initial"

$TERMO send-keys -X scroll-top || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$initial"

$TERMO send-keys -X recentre-top-bottom || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$initial"

$TERMO send-keys -X other-end || exit 1
$TERMO send-keys -X cursor-down || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1

extended_end=$(printf 'line 10 xxxxxxxxxx\nline 11 xxxxxxxxxx\nline 12 xxxxxxxxxx')
expect_buffer "$extended_end"

$TERMO send-keys -X stop-selection || exit 1
$TERMO send-keys -X other-end || exit 1
$TERMO send-keys -X other-end || exit 1
$TERMO send-keys -X cursor-up || exit 1
$TERMO send-keys -X copy-selection-no-clear || exit 1

extended_start=$(printf 'line 09 xxxxxxxxxx\nline 10 xxxxxxxxxx\nline 11 xxxxxxxxxx\nline 12 xxxxxxxxxx')
expect_buffer "$extended_start"

$TERMO2 new-session -d -x40 -y10 "$TERMO attach" || exit 1
sleep 1
OUTER=$($TERMO2 list-panes -F '#{pane_id}' | head -1)
[ -n "$OUTER" ] || fail "no outer pane"

wheel 65 5 5
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$extended_start"

wheel 64 5 5
$TERMO send-keys -X copy-selection-no-clear || exit 1
expect_buffer "$extended_start"

exit 0
