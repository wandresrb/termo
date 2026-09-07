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

assert_unchanged()
{
	option=$1
	expected=$2
	i=0

	while [ $i -lt 15 ]; do
		value=$($TERMO show -gqv "$option" 2>/dev/null || true)
		[ "$value" = "$expected" ] || \
			fail "expected $option to remain '$expected' but got '$value'"
		i=$((i + 1))
		sleep 0.2
	done
}

$TERMO new -d -s one || fail "new-session failed"

$TERMO set -g @seen 0 || fail "set @seen failed"
$TERMO set-hook -g -B '@session-name::#{session_name}' \
	'set -g @seen "#{hook}:#{hook_value}"' ||
	fail "set-hook -B failed"
shown=$($TERMO show-hooks -g -B @session-name) ||
	fail "show-hooks -B failed"
[ "$shown" = '@session-name::#{session_name}' ] ||
	fail "unexpected show-hooks -B output: $shown"
shown=$($TERMO show-hooks -g -BF \
	'#{option_name}:#{hook_monitor_target}:#{hook_monitor_format}:#{option_value}:#{option_is_hook}:#{option_is_user}' \
	@session-name) ||
	fail "show-hooks -BF failed"
[ "$shown" = '@session-name::#{session_name}:@session-name::#{session_name}:1:1' ] ||
	fail "unexpected show-hooks -BF output: $shown"
shown=$($TERMO show-hooks -g) ||
	fail "show-hooks -g failed"
echo "$shown" | grep -q '^@session-name ' ||
	fail "show-hooks -g did not show monitor hook: $shown"
assert_unchanged @seen 0

$TERMO rename-session two || fail "rename-session two failed"
wait_for @seen '@session-name:two'
shown=$($TERMO show-hooks -g -BF '#{hook_fire_count}:#{t/p:hook_fire_time}' \
	@session-name) ||
	fail "show-hooks -BF fire failed"
echo "$shown" | grep -q '^1:[^-][^ ]*$' ||
	fail "unexpected show-hooks -BF fire output: $shown"

$TERMO set -g @seen-last 0 || fail "set @seen-last failed"
$TERMO set-hook -g -B '@session-name::#{session_name}' \
	'set -g @seen-last "#{hook_last}->#{hook_value}"' ||
	fail "set-hook -B replacement failed"
assert_unchanged @seen-last 0
$TERMO rename-session one || fail "rename-session one failed"
wait_for @seen-last 'two->one'

$TERMO set-hook -gu -B @session-name || fail "set-hook -gu -B failed"
shown=$($TERMO show-hooks -g -B @session-name) ||
	fail "show-hooks -B after remove failed"
[ -z "$shown" ] || fail "show-hooks -B showed removed monitor: $shown"
last=$($TERMO show -gqv @seen-last)
$TERMO rename-session three || fail "rename-session three failed"
assert_unchanged @seen-last "$last"

$TERMO set -gu @value || fail "unset @value failed"
$TERMO set -g @empty-seen 0 || fail "set @empty-seen failed"
$TERMO set-hook -g -B '@empty::#{@value}' \
	'set -g @empty-seen "#{hook_last}->#{hook_value}"' ||
	fail "set-hook -B empty failed"
assert_unchanged @empty-seen 0
$TERMO set -g @value changed || fail "set @value failed"
wait_for @empty-seen '->changed'

if $TERMO set-hook -g -B 'bad::#{session_name}' 'display-message x' \
	>"$OUT/bad.out" 2>"$OUT/bad.err"; then
	fail "non-@ monitor hook name was accepted"
fi

session=$($TERMO display -p '#{session_id}')
window=$($TERMO display -p '#{window_id}')
pane=$($TERMO display -p '#{pane_id}')
pane_number=${pane#%}

$TERMO set -gu @pane-value || fail "unset @pane-value failed"
$TERMO set -g @pane-seen 0 || fail "set @pane-seen failed"
$TERMO set-hook -g -B "@pane:%$pane_number:#{pane_width}" \
	'set -g @pane-seen "#{hook_session}:#{hook_window}:#{hook_window_index}:#{hook_pane}:#{hook_value}"' ||
	fail "set-hook -B pane selector failed"
shown=$($TERMO show-hooks -g -BF \
	'#{option_name}:#{hook_monitor_target}:#{hook_monitor_format}' @pane) ||
	fail "show-hooks -BF pane failed"
[ "$shown" = "@pane:%$pane_number:#{pane_width}" ] ||
	fail "unexpected show-hooks -BF pane output: $shown"
assert_unchanged @pane-seen 0
$TERMO set-hook -g -B "@pane:%$pane_number:#{@pane-value}" \
	'set -g @pane-seen "#{hook_session}:#{hook_window}:#{hook_window_index}:#{hook_pane}:#{hook_value}"' ||
	fail "set-hook -B pane replacement failed"
assert_unchanged @pane-seen 0
$TERMO set -g @pane-value changed || fail "set @pane-value failed"
wait_for @pane-seen "$session:$window:0:$pane:changed"

$TERMO set -g @exact-value one || fail "set @exact-value failed"
$TERMO set -g @exact-seen 0 || fail "set @exact-seen failed"
$TERMO set -gw @foo 'set -g @exact-seen inherited' ||
	fail "set global @foo failed"
$TERMO set-hook -w -B '@foo::#{@exact-value}' ||
	fail "set-hook -B exact scope monitor failed"
assert_unchanged @exact-seen 0
$TERMO set -g @exact-value two || fail "set @exact-value two failed"
assert_unchanged @exact-seen 0
$TERMO set-hook -w -B '@foo::#{@exact-value}' \
	'set -g @exact-seen "#{hook_value}"' ||
	fail "set-hook -B exact scope command failed"
assert_unchanged @exact-seen 0
$TERMO set -g @exact-value three || fail "set @exact-value three failed"
wait_for @exact-seen three

target_pane=$($TERMO splitw -P -F '#{pane_id}') ||
	fail "split-window failed"
$TERMO set -g @target-pane 0 || fail "set @target-pane failed"
$TERMO set-hook -g -B '@target:%*:#{@target-value}' \
	'set -g @target-pane "#{pane_id}"' ||
	fail "set-hook -B target pane failed"
assert_unchanged @target-pane 0
$TERMO set -pt "$target_pane" @target-value changed ||
	fail "set pane @target-value failed"
wait_for @target-pane "$target_pane"

$TERMO new -d -s zzz-survivor || fail "new survivor session failed"
$TERMO set -g @global-after-destroy 0 ||
	fail "set @global-after-destroy failed"
$TERMO set-hook -g -t three -B '@global-after-destroy-session::#{session_name}' \
	'set -g @global-after-destroy "#{hook_last}->#{hook_value}"' ||
	fail "set-hook -B global after destroy failed"
assert_unchanged @global-after-destroy 0
$TERMO kill-session -t three || fail "kill destroyed monitor session failed"
wait_for @global-after-destroy 'three->zzz-survivor'

exit 0
