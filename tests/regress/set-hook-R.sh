#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen
LC_ALL=C.UTF-8
LANG=C.UTF-8
export TERM LC_ALL LANG

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
OUT=$(mktemp -d)
TERMO_TMPDIR="$OUT"
export TERMO_TMPDIR
TMUX_TMPDIR="$TERMO_TMPDIR"
export TMUX_TMPDIR
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"

fail()
{
	echo "$*" >&2
	$TERMO kill-server 2>/dev/null || true
	rm -rf "$OUT"
	exit 1
}

cleanup()
{
	$TERMO kill-server 2>/dev/null || true
	rm -rf "$OUT"
}
trap cleanup EXIT

wait_for()
{
	option=$1
	expected=$2
	i=0

	while [ $i -lt 30 ]; do
		value=$($TERMO show -gqv "$option" 2>/dev/null || true)
		[ "$value" = "$expected" ] && return 0
		i=$((i + 1))
		sleep 0.2
	done
	fail "expected $option to be '$expected' but got '$value'"
}

$TERMO new -d -s one || fail "new-session one failed"
$TERMO new -d -s two || fail "new-session two failed"

pane=$($TERMO display -pt two:0.0 '#{pane_id}') ||
	fail "display-message pane failed"

$TERMO set -g @seen 0 || fail "set @seen failed"
$TERMO set-hook -g @manual \
	'set -gF @seen "#{hook}:#{session_name}:#{window_index}:#{pane_id}"' ||
	fail "set-hook @manual failed"

[ "$($TERMO show -gqv @seen)" = 0 ] ||
	fail "hook ran before set-hook -R"

$TERMO set-hook -g -R -t two:0.0 @manual ||
	fail "set-hook -R @manual failed"
wait_for @seen "@manual:two:0:$pane"

exit 0
