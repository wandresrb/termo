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
$TERMO new -d -s two || fail "new-session two failed"

# An after hook fires with the command arguments as formats.
$TERMO set -g @after 0 || fail "set @after failed"
$TERMO set-hook -g after-rename-window \
	'set -gF @after "#{hook}|#{hook_arguments}|#{hook_argument_0}|#{hook_flag_t}"' ||
	fail "set-hook -g after-rename-window failed"
$TERMO rename-window -t one:0 first || fail "rename-window first failed"
wait_for @after 'after-rename-window|-t one:0 first|first|one:0'

# An appended hook command runs after the first.
$TERMO set -g @after2 0 || fail "set @after2 failed"
$TERMO set-hook -ga after-rename-window \
	'set -gF @after2 "#{@after}+2"' ||
	fail "set-hook -ga after-rename-window failed"
$TERMO rename-window -t one:0 second || fail "rename-window second failed"
wait_for @after 'after-rename-window|-t one:0 second|second|one:0'
wait_for @after2 'after-rename-window|-t one:0 second|second|one:0+2'
$TERMO set-hook -gu after-rename-window || fail "set-hook -gu failed"

# A hook command is inserted after the command that fired it, before the next
# command in the same command list.
$TERMO set -g @order '' || fail "set @order failed"
$TERMO set-hook -g after-rename-window \
	'set -gF @order "#{@order}H"' ||
	fail "set-hook -g after-rename-window order failed"
$TERMO rename-window -t one:0 ordered \; set -gF @order '#{@order}N' ||
	fail "rename-window order failed"
wait_for @order 'HN'
$TERMO set-hook -gu after-rename-window || fail "set-hook -gu order failed"

# Repeated flag values are available as numbered hook flag formats.
$TERMO set -g @flags 0 || fail "set @flags failed"
$TERMO set-hook -g after-split-window \
	'set -gF @flags "#{hook_arguments}|#{hook_argument_0}|#{hook_flag_t}|#{hook_flag_e}|#{hook_flag_e_0}|#{hook_flag_e_1}"' ||
	fail "set-hook -g after-split-window failed"
$TERMO split-window -d -t one:0 -e A=1 -e B=2 'sleep 60' ||
	fail "split-window flags failed"
wait_for @flags '-d -e A=1 -e B=2 -t one:0 "sleep 60"|sleep 60|one:0|B=2|A=1|B=2'
$TERMO set-hook -gu after-split-window ||
	fail "set-hook -gu after-split-window failed"

# A session after hook only fires for commands targeting that session and
# the hook commands run with the command target as current state.
$TERMO set -g @safter 0 || fail "set @safter failed"
$TERMO set-hook -t two after-rename-window \
	'set -gF @safter "#{hook}:#{session_name}"' ||
	fail "set-hook -t two after-rename-window failed"
$TERMO rename-window -t one:0 third || fail "rename-window third failed"
assert_unchanged @safter 0
$TERMO rename-window -t two:0 fourth || fail "rename-window fourth failed"
wait_for @safter 'after-rename-window:two'
$TERMO set-hook -u -t two after-rename-window ||
	fail "set-hook -u -t two failed"

# The command-error hook fires when a command fails.
$TERMO set -g @error 0 || fail "set @error failed"
$TERMO set-hook -g command-error 'set -gF @error "#{hook}"' ||
	fail "set-hook -g command-error failed"
if $TERMO rename-window -t nosuchsession:0 x 2>/dev/null; then
	fail "rename-window to missing session succeeded"
fi
wait_for @error 'command-error'
$TERMO set-hook -gu command-error || fail "set-hook -gu command-error failed"

# Commands run from a hook do not fire their own after hooks.
$TERMO set -g @copy 0 || fail "set @copy failed"
$TERMO set-hook -g after-copy-mode 'set -gF @copy "#{hook}"' ||
	fail "set-hook -g after-copy-mode failed"
$TERMO copy-mode -t one:0 || fail "copy-mode failed"
wait_for @copy 'after-copy-mode'
$TERMO send-keys -t one:0 -X cancel || fail "cancel failed"
$TERMO set -g @copy 0 || fail "reset @copy failed"
$TERMO set -g @ran 0 || fail "set @ran failed"
$TERMO set-hook -g after-rename-window 'copy-mode -t one:0' ||
	fail "set-hook -g after-rename-window nested failed"
$TERMO set-hook -ga after-rename-window 'set -g @ran 1' ||
	fail "set-hook -ga after-rename-window nested failed"
$TERMO rename-window -t one:0 fifth || fail "rename-window fifth failed"
wait_for @ran 1
mode=$($TERMO display -pt one:0 '#{pane_in_mode}') ||
	fail "display pane_in_mode failed"
[ "$mode" = 1 ] || fail "hook did not enter copy mode"
assert_unchanged @copy 0

exit 0
