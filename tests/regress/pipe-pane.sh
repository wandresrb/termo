#!/bin/sh

# Tests of pipe-pane behaviour.

PATH=/bin:/usr/bin
TERM=screen
LANG=C.UTF-8
LC_ALL=C.UTF-8
export TERM LANG LC_ALL

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

fail()
{
	echo "$1"
	$TERMO kill-server 2>/dev/null
	exit 1
}

check_ok()
{
	if ! $TERMO "$@"; then
		fail "Command failed: $*"
	fi
}

check_alive()
{
	if [ "$($TERMO display-message -p alive 2>&1)" != "alive" ]; then
		fail "Server died"
	fi
}

# A pipe-pane -I child may write after the pane process has exited. With
# remain-on-exit, the pane stays around but its bufferevent has been freed.
check_ok new-session -d -s pipe -x 80 -y 24 'sleep 1'
check_ok set-option -t pipe:0 remain-on-exit on
check_ok pipe-pane -t pipe:0.0 -I 'sleep 2; printf x'

i=0
while [ "$($TERMO display-message -p -t pipe:0.0 '#{pane_dead}')" != "1" ]; do
	i=$((i + 1))
	[ "$i" -gt 50 ] && fail "Pane did not die"
	sleep 0.1
done

sleep 2
check_alive

$TERMO kill-server 2>/dev/null
