#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

cleanup()
{
	$TERMO kill-server 2>/dev/null
}
trap cleanup 0
trap 'exit 1' 1 2 3 15

$TERMO new -d -x40 -y10 \
	'i=0; while [ $i -lt 80 ]; do echo "line $i"; i=$((i + 1)); done; cat' ||
	exit 1
$TERMO set -g window-size manual || exit 1

$TERMO copy-mode -e || exit 1
$TERMO send-keys -X history-top || exit 1
$TERMO send-keys -X start-of-line || exit 1
$TERMO send-keys -X begin-selection || exit 1
$TERMO send-keys -X cursor-down || exit 1

[ "$($TERMO display-message -p '#{selection_present}')" = "1" ] || exit 1
$TERMO send-keys -N200 -X scroll-down || exit 1
[ "$($TERMO display-message -p '#{pane_in_mode} #{scroll_position}')" = "1 0" ] ||
	exit 1

$TERMO send-keys -X clear-selection || exit 1
$TERMO send-keys -X scroll-down || exit 1
[ "$($TERMO display-message -p '#{pane_in_mode}')" = "0" ] || exit 1

exit 0
