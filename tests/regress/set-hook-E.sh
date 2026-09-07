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

wait_channel()
{
	channel=$1

	if command -v timeout >/dev/null 2>&1; then
		timeout 10 $TERMO wait-for "$channel" ||
			fail "wait-for $channel timed out"
		return
	fi

	$TERMO wait-for "$channel" &
	pid=$!
	i=0
	while kill -0 "$pid" 2>/dev/null; do
		[ $i -lt 50 ] || {
			kill "$pid" 2>/dev/null || true
			fail "wait-for $channel timed out"
		}
		i=$((i + 1))
		sleep 0.2
	done
	wait "$pid" || fail "wait-for $channel failed"
}

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

assert_unchanged()
{
	option=$1
	expected=$2
	count=${3:-15}
	i=0

	while [ $i -lt "$count" ]; do
		value=$($TERMO show -gqv "$option" 2>/dev/null || true)
		[ "$value" = "$expected" ] ||
			fail "expected $option to remain '$expected' but got '$value'"
		i=$((i + 1))
		sleep 0.2
	done
}

wait_list()
{
	name=$1
	i=0

	while [ $i -lt 50 ]; do
		value=$($TERMO wait-for -E -l "$name" 2>/dev/null || true)
		if [ -n "$value" ]; then
			printf '%s\n' "$value" | sed -n '1p'
			return
		fi
		i=$((i + 1))
		sleep 0.2
	done
	fail "wait-for -E -l $name found no waiters"
}

$TERMO new -d -s one || fail "new-session one failed"
$TERMO new -d -s two || fail "new-session two failed"

$TERMO set -g @event_seen 0 || fail "set @event_seen failed"
$TERMO wait-for -E @manual-event \; set -g @event_seen 1 \; \
	wait-for -S she-event &
event_pid=$!
wait_list @manual-event >/dev/null

$TERMO set-hook -E @manual-event || fail "set-hook -E @manual-event failed"
wait_channel she-event
wait "$event_pid" || fail "wait-for -E @manual-event failed"
wait_for @event_seen 1

$TERMO set-hook -E @no-sink || fail "set-hook -E @no-sink failed"

pane=$($TERMO display -pt two:0.0 '#{pane_id}') ||
	fail "display-message pane failed"
$TERMO set -g @hook_seen 0 || fail "set @hook_seen failed"
$TERMO set-hook -g @manual-hook \
	'set -gF @hook_seen "#{hook}:#{session_name}:#{window_index}:#{pane_id}"' ||
	fail "set-hook @manual-hook failed"

$TERMO set-hook -E -t two:0.0 @manual-hook ||
	fail "set-hook -E @manual-hook failed"
wait_for @hook_seen "@manual-hook:two:0:$pane"

$TERMO set -g @r_hook 0 || fail "set @r_hook failed"
$TERMO set -g @r_event 0 || fail "set @r_event failed"
$TERMO set-hook -g @manual-r 'set -g @r_hook 1' ||
	fail "set-hook @manual-r failed"
$TERMO wait-for -E @manual-r \; set -g @r_event 1 \; wait-for -S she-r &
r_pid=$!
r_client=$(wait_list @manual-r)

$TERMO set-hook -R @manual-r || fail "set-hook -R @manual-r failed"
wait_for @r_hook 1
assert_unchanged @r_event 0 5

$TERMO wait-for -E -w "$r_client" @manual-r ||
	fail "wait-for -E -w @manual-r failed"
wait_channel she-r
wait "$r_pid" || fail "wait-for -E @manual-r failed"

if $TERMO set-hook -E window-renamed 2>/dev/null; then
	fail "set-hook -E window-renamed succeeded"
fi

exit 0
