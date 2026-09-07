#!/bin/sh

# End-of-life hooks: pane-exited, pane-died, window-unlinked and
# session-closed when the pane, window or session they refer to is being
# or has been destroyed. Each hook appends to @log so the order hooks fire
# in is checked as well as the hook formats for the dead objects.

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

$TERMO new -d -s main || fail "new-session main failed"

$TERMO set -g @log '' || fail "set @log failed"
$TERMO set-hook -g pane-exited \
	'set -gF @log "#{@log}|pane-exited:#{hook_pane}"' ||
	fail "set-hook pane-exited failed"
$TERMO set-hook -g window-unlinked \
	'set -gF @log "#{@log}|window-unlinked:#{hook_session_name}:#{hook_window_name}"' ||
	fail "set-hook window-unlinked failed"
$TERMO set-hook -g session-closed \
	'set -gF @log "#{@log}|session-closed:#{hook_session_name}"' ||
	fail "set-hook session-closed failed"

# The only pane of the only window of a session exits: pane-exited, then
# window-unlinked, then session-closed, each seeing the dead object in the
# hook formats.
pane=$($TERMO new -d -s doomed -n dwin -P -F '#{pane_id}' 'true') ||
	fail "new-session doomed failed"
wait_for @log \
	"|pane-exited:$pane|window-unlinked:doomed:dwin|session-closed:doomed"
assert_unchanged @log \
	"|pane-exited:$pane|window-unlinked:doomed:dwin|session-closed:doomed"

# The dead pane, window and session cannot be used as targets but the
# server survives.
if $TERMO select-pane -t "$pane" 2>/dev/null; then
	fail "dead pane still a valid target"
fi
if $TERMO list-windows -t doomed >/dev/null 2>&1; then
	fail "dead session still a valid target"
fi
$TERMO list-panes -sat main >/dev/null || fail "list-panes failed"
$TERMO has -t main || fail "server died after pane exit chain"

# A pane-exited hook command can run after the pane has been removed. It
# should not retain only the event payload's temporary target references.
$TERMO set -g @queued-pane-exited 0 || fail "set @queued-pane-exited failed"
$TERMO set-hook -g pane-exited \
	'display-message -p "queued #{hook_pane}" ; set -g @queued-pane-exited 1' ||
	fail "set-hook queued pane-exited failed"
pane=$($TERMO new -d -s queued -n qwin -P -F '#{pane_id}' 'true') ||
	fail "new-session queued failed"
wait_for @queued-pane-exited 1
$TERMO has -t main || fail "server died after queued pane-exited hook"
$TERMO set-hook -g pane-exited \
	'set -gF @log "#{@log}|pane-exited:#{hook_pane}"' ||
	fail "restore pane-exited hook failed"

# kill-window on the last window: window-unlinked then session-closed but
# no pane-exited for the panes in the killed window.
$TERMO set -g @log '' || fail "reset @log failed"
$TERMO new -d -s doomed2 -n dwin2 || fail "new-session doomed2 failed"
$TERMO splitw -d -t doomed2:0 || fail "split-window doomed2 failed"
$TERMO kill-window -t doomed2:0 || fail "kill-window failed"
wait_for @log '|window-unlinked:doomed2:dwin2|session-closed:doomed2'
assert_unchanged @log '|window-unlinked:doomed2:dwin2|session-closed:doomed2'
$TERMO has -t main || fail "server died after kill-window chain"

# kill-session: session-closed fires first, then window-unlinked for its
# windows. A window linked into another session survives.
$TERMO new -d -s shareA -n shared || fail "new-session shareA failed"
$TERMO new -d -s shareB -n bwin || fail "new-session shareB failed"
$TERMO link-window -s shareA:shared -t shareB:7 || fail "link-window failed"
$TERMO set -g @log '' || fail "reset @log failed"
$TERMO kill-session -t shareA || fail "kill-session shareA failed"
wait_for @log '|session-closed:shareA|window-unlinked:shareA:shared'
name=$($TERMO display -pt shareB:7 '#{window_name}') ||
	fail "shared window did not survive"
[ "$name" = shared ] || fail "expected window shared but got $name"

# Killing the surviving session destroys the shared window for real while
# the session is being destroyed.
$TERMO set -g @log '' || fail "reset @log failed"
$TERMO kill-window -t shareB:0 || fail "kill-window bwin failed"
wait_for @log '|window-unlinked:shareB:bwin'
$TERMO set -g @log '' || fail "reset @log failed"
$TERMO kill-session -t shareB || fail "kill-session shareB failed"
wait_for @log '|session-closed:shareB|window-unlinked:shareB:shared'
$TERMO has -t main || fail "server died after kill-session chain"

# A pane-died hook can kill its own dead pane (the hook runs with the dead
# pane as current target). The kill-pane runs without hooks so pane-exited
# does not fire.
$TERMO set -g @log '' || fail "reset @log failed"
$TERMO new -d -s roe -n rwin || fail "new-session roe failed"
$TERMO set -wt roe:0 remain-on-exit on || fail "set remain-on-exit failed"
$TERMO set-hook -g pane-died \
	'set -gF @log "#{@log}|pane-died:#{hook_pane}" ; kill-pane' ||
	fail "set-hook pane-died failed"
pane=$($TERMO splitw -d -t roe:0 -P -F '#{pane_id}' 'true') ||
	fail "split-window roe failed"
wait_for @log "|pane-died:$pane"
assert_unchanged @log "|pane-died:$pane"
if $TERMO select-pane -t "$pane" 2>/dev/null; then
	fail "pane-died hook did not kill its pane"
fi
$TERMO list-panes -t roe:0 >/dev/null || fail "surviving pane broken"
$TERMO has -t roe || fail "session roe died"
$TERMO set-hook -gu pane-died || fail "unset pane-died failed"
$TERMO kill-session -t roe || fail "kill-session roe failed"

# Killing the last session with end-of-life hooks still set: the server
# runs the hooks and exits cleanly.
$TERMO kill-session -t main || fail "kill-session main failed"
i=0
while $TERMO has 2>/dev/null; do
	i=$((i + 1))
	[ $i -lt 30 ] || fail "server still running after last session killed"
	sleep 0.2
done

exit 0
