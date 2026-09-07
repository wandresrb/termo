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
	event=$1
	name=$2
	i=0

	while [ $i -lt 50 ]; do
		if [ "$event" = 1 ]; then
			value=$($TERMO wait-for -E -l "$name" 2>/dev/null || true)
		else
			value=$($TERMO wait-for -l "$name" 2>/dev/null || true)
		fi
		if [ -n "$value" ]; then
			printf '%s\n' "$value" | sed -n '1p'
			return
		fi
		i=$((i + 1))
		sleep 0.2
	done
	fail "wait-for -l $name found no waiters"
}

$TERMO new -d -s wf || fail "new-session failed"

$TERMO wait-for wf-list &
list_pid=$!
client=$(wait_list 0 wf-list)
$TERMO wait-for -w "$client" wf-list || fail "wait-for -w wf-list failed"
wait "$list_pid" || fail "wait-for -w did not wake channel waiter"

$TERMO set -g @forced 0 || fail "set @forced failed"
$TERMO wait-for -E @forced-event \; set -g @forced 1 &
forced_pid=$!
client=$(wait_list 1 @forced-event)
$TERMO wait-for -E -w "$client" @forced-event ||
	fail "wait-for -E -w @forced-event failed"
wait "$forced_pid" || fail "wait-for -E -w did not wake event waiter"
[ "$($TERMO show -gqv @forced)" = 1 ] ||
	fail "wait-for -E -w did not continue event waiter"

if $TERMO wait-for -E foobar 2>/dev/null; then
	fail "wait-for -E accepted invalid event"
fi

$TERMO wait-for -E @not-yet-fired &
not_yet_pid=$!
client=$(wait_list 1 @not-yet-fired)
$TERMO wait-for -E -w "$client" @not-yet-fired ||
	fail "wait-for -E -w @not-yet-fired failed"
wait "$not_yet_pid" || fail "wait-for -E @not-yet-fired failed"

$TERMO set -g @wf_value 0 || fail "set @wf_value failed"
$TERMO set-hook -g -B '@wf::#{@wf_value}' 'wait-for -S wf-hook' ||
	fail "set-hook -B failed"

$TERMO wait-for -E @wf \; wait-for -S wf-event &
event_pid=$!

# Let the monitor take its first sample so the next change is reported.
sleep 1.5

$TERMO set -g @wf_value 1 || fail "set @wf_value 1 failed"

wait_channel wf-event
wait_channel wf-hook
wait "$event_pid" || fail "wait-for -E command failed"

$TERMO set -g @late 0 || fail "set @late failed"
$TERMO wait-for -E @wf \; set -g @late 1 \; wait-for -S wf-late &
late_pid=$!
assert_unchanged @late 0 5

$TERMO set -g @wf_value 2 || fail "set @wf_value 2 failed"
wait_channel wf-late
wait "$late_pid" || fail "late wait-for -E command failed"

$TERMO set -g @filtered 0 || fail "set @filtered failed"
$TERMO wait-for -E -F '#{==:#{value},3}' @wf \; set -g @filtered 1 \; \
	wait-for -S wf-filtered &
filtered_pid=$!
assert_unchanged @filtered 0 5

$TERMO set -g @wf_value unmatched || fail "set @wf_value unmatched failed"
assert_unchanged @filtered 0 5

$TERMO set -g @wf_value 3 || fail "set @wf_value 3 failed"
wait_channel wf-filtered
wait "$filtered_pid" || fail "filtered wait-for -E command failed"

verbose_file="$OUT/verbose"
$TERMO wait-for -E -v @wf \; wait-for -S wf-verbose >"$verbose_file" &
verbose_pid=$!

sleep 0.5
$TERMO set -g @wf_value 4 || fail "set @wf_value 4 failed"
wait_channel wf-verbose
wait "$verbose_pid" || fail "verbose wait-for -E command failed"
grep '^event=@wf$' "$verbose_file" >/dev/null ||
	fail "verbose wait-for -E did not print event payload"
grep '^value=4$' "$verbose_file" >/dev/null ||
	fail "verbose wait-for -E did not print value payload"
grep '^_hook_monitor=' "$verbose_file" >/dev/null &&
	fail "verbose wait-for -E printed private payload"

$TERMO new -d -s wf2 || fail "new-session wf2 failed"

$TERMO wait-for -E window-renamed \; wait-for -S wf-renamed &
renamed_pid=$!

sleep 0.5
$TERMO rename-window -t wf2:0 renamed || fail "rename-window failed"
wait_channel wf-renamed
wait "$renamed_pid" || fail "wait-for -E window-renamed failed"

$TERMO set-hook -g window-renamed 'wait-for -S wf-hook-renamed' ||
	fail "set-hook window-renamed failed"
$TERMO rename-window -t wf2:0 renamed-again ||
	fail "rename-window renamed-again failed"
wait_channel wf-hook-renamed

$TERMO set -g @builtin_filtered 0 || fail "set @builtin_filtered failed"
target=$($TERMO splitw -d -t wf2:0 -P -F '#{pane_id}' 'sleep 30') ||
	fail "split-window target failed"
$TERMO wait-for -E -F "#{==:#{pane},$target}" pane-exited \; \
	set -g @builtin_filtered 1 \; wait-for -S wf-builtin-filtered &
builtin_filtered_pid=$!
assert_unchanged @builtin_filtered 0 5

$TERMO splitw -d -t wf2:0 'true' || fail "split-window nonmatching failed"
assert_unchanged @builtin_filtered 0 5

$TERMO send-keys -t "$target" C-c || fail "send C-c to target failed"
wait_channel wf-builtin-filtered
wait "$builtin_filtered_pid" ||
	fail "filtered wait-for -E pane-exited command failed"

exit 0
