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

	while [ $i -lt 10 ]; do
		value=$($TERMO show -gqv "$option" 2>/dev/null || true)
		[ "$value" = "$expected" ] || \
			fail "expected $option to remain '$expected' but got '$value'"
		i=$((i + 1))
		sleep 0.2
	done
}

$TERMO new -d -s one || fail "new-session one failed"

# A global hook fires and sees the hook formats.
$TERMO set -g @created 0 || fail "set @created failed"
$TERMO set-hook -g session-created \
	'set -gF @created "#{hook}:#{hook_session_name}"' ||
	fail "set-hook -g session-created failed"
$TERMO new -d -s two || fail "new-session two failed"
wait_for @created 'session-created:two'

# Hooks are arrays: an appended command runs after the first.
$TERMO set -g @second 0 || fail "set @second failed"
$TERMO set-hook -ga session-created \
	'set -gF @second "#{@created}+second"' ||
	fail "set-hook -ga session-created failed"
$TERMO new -d -s three || fail "new-session three failed"
wait_for @created 'session-created:three'
wait_for @second 'session-created:three+second'

# show-hooks lists both commands.
shown=$($TERMO show-hooks -g session-created) ||
	fail "show-hooks -g failed"
[ "$(echo "$shown" | wc -l)" -eq 2 ] ||
	fail "unexpected show-hooks output: $shown"
echo "$shown" | grep -q '^session-created\[0\]' ||
	fail "missing first array item: $shown"
echo "$shown" | grep -q '^session-created\[1\]' ||
	fail "missing second array item: $shown"
shown=$($TERMO show-hooks -gF '#{option_name}:#{hook_fire_count}:#{t/p:hook_fire_time}' \
	session-created) ||
	fail "show-hooks -gF failed"
echo "$shown" | grep -q '^session-created:[1-9][0-9]*:[^-][^ ]*$' ||
	fail "missing hook fire formats: $shown"

# User hooks are options, but show-hooks should list only registered @ hooks
# and not ordinary user options.
$TERMO set -g @not-a-hook value || fail "set @not-a-hook failed"
$TERMO set-hook -g @user-hook 'lsk' ||
	fail "set-hook @user-hook failed"
shown=$($TERMO show-hooks -g) ||
	fail "show-hooks -g all failed"
echo "$shown" | grep -q '^@user-hook lsk$' ||
	fail "missing user hook: $shown"
if echo "$shown" | grep -q '^@not-a-hook '; then
	fail "show-hooks listed user option: $shown"
fi

# Unsetting removes the whole hook.
$TERMO set-hook -gu session-created || fail "set-hook -gu failed"
shown=$($TERMO show-hooks -g session-created) ||
	fail "show-hooks -g after unset failed"
if echo "$shown" | grep -q '\['; then
	fail "show-hooks showed removed hook: $shown"
fi
$TERMO set -g @created 0 || fail "reset @created failed"
$TERMO new -d -s four || fail "new-session four failed"
assert_unchanged @created 0

# An unknown hook name is rejected.
if $TERMO set-hook -g no-such-hook 'display x' 2>/dev/null; then
	fail "unknown hook name was accepted"
fi

# A session hook only fires for events in that session.
$TERMO set -g @renamed 0 || fail "set @renamed failed"
$TERMO set-hook -t one window-renamed \
	'set -gF @renamed "#{hook}:#{hook_window_name}"' ||
	fail "set-hook -t one window-renamed failed"
$TERMO rename-window -t two:0 twoname || fail "rename-window two failed"
assert_unchanged @renamed 0
$TERMO rename-window -t one:0 onename || fail "rename-window one failed"
wait_for @renamed 'window-renamed:onename'
$TERMO set-hook -u -t one window-renamed || fail "set-hook -u failed"

# A pane hook only fires for events in that pane.
pane2=$($TERMO splitw -d -t one:0 -P -F '#{pane_id}') ||
	fail "split-window failed"
$TERMO set -g @mode 0 || fail "set @mode failed"
$TERMO set-hook -p -t "$pane2" pane-mode-changed \
	'set -gF @mode "#{hook}:#{hook_pane}"' ||
	fail "set-hook -p pane-mode-changed failed"
$TERMO copy-mode -t one:0.0 || fail "copy-mode pane 0 failed"
assert_unchanged @mode 0
$TERMO copy-mode -t "$pane2" || fail "copy-mode pane 1 failed"
wait_for @mode "pane-mode-changed:$pane2"
$TERMO send-keys -t "$pane2" -X cancel || fail "cancel failed"
$TERMO set-hook -pu -t "$pane2" pane-mode-changed ||
	fail "set-hook -pu failed"

# A window hook only fires for events in that window.
$TERMO set -g @wmode 0 || fail "set @wmode failed"
$TERMO set-hook -w -t two:0 pane-mode-changed \
	'set -gF @wmode "#{hook}:#{hook_window_name}"' ||
	fail "set-hook -w pane-mode-changed failed"
$TERMO copy-mode -t three:0.0 || fail "copy-mode three failed"
assert_unchanged @wmode 0
$TERMO copy-mode -t two:0.0 || fail "copy-mode two failed"
wait_for @wmode 'pane-mode-changed:twoname'
$TERMO set-hook -wu -t two:0 pane-mode-changed ||
	fail "set-hook -wu failed"

# Commands run from a hook do not fire hooks again.
$TERMO set -g @renames '' || fail "set @renames failed"
$TERMO set-hook -g window-renamed \
	'set -gF @renames "#{@renames}x" ; rename-window -t three:0 inner' ||
	fail "set-hook -g window-renamed failed"
$TERMO rename-window -t three:0 outer || fail "rename-window outer failed"
wait_for @renames 'x'
assert_unchanged @renames 'x'
name=$($TERMO display -pt three:0 '#{window_name}') ||
	fail "display window_name failed"
[ "$name" = inner ] || fail "expected window name inner but got $name"

exit 0
