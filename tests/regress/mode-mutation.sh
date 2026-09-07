#!/bin/sh

# Exercise modes while their backing objects are changed from outside the
# client displaying the mode. This catches stale selection indexes and pointers
# after a mode list is shrunk or rebuilt.

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TMP=$(mktemp -d) || exit 1
TERMO_TMPDIR="$TMP"
export TERMO_TMPDIR
TMUX_TMPDIR="$TERMO_TMPDIR"
export TMUX_TMPDIR

TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"

TMUX="$TERMO"
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"

cleanup_servers()
{
	$TERMO kill-server 2>/dev/null
	$TERMO2 kill-server 2>/dev/null
	sleep 0.5
}

cleanup()
{
	cleanup_servers
	rm -rf "$TMP"
}
trap cleanup EXIT

fail()
{
	echo "$1" >&2
	cleanup
	exit 1
}

capture()
{
	$TERMO2 capture-pane -p -t out:0 2>/dev/null
}

assert_alive()
{
	$TERMO display-message -p 'alive' >/dev/null 2>&1 || \
		fail "$1: server exited"
}

wait_clients()
{
	i=0
	while [ "$i" -lt 50 ]; do
		c=$($TERMO list-clients -F x 2>/dev/null | grep -c x)
		[ "$c" -eq "$1" ] && return 0
		sleep 0.2
		i=$((i + 1))
	done
	fail "expected $1 clients, have $c"
}

wait_for()
{
	i=0
	while [ "$i" -lt 50 ]; do
		capture | grep -F -q "$1" && return 0
		sleep 0.2
		i=$((i + 1))
	done
	fail "timed out waiting for '$1'"
}

wait_mode()
{
	t=$1
	want=$2

	i=0
	while [ "$i" -lt 50 ]; do
		got=$($TERMO display-message -p -t "$t" '#{pane_in_mode}' \
		    2>/dev/null)
		[ "$got" = "$want" ] && return 0
		sleep 0.2
		i=$((i + 1))
	done
	fail "pane $t mode state is $got, expected $want"
}

repeat_key()
{
	t=$1
	key=$2
	count=$3

	i=0
	while [ "$i" -lt "$count" ]; do
		$TERMO send-keys -t "$t" "$key" || \
			fail "failed to send $key to $t"
		i=$((i + 1))
	done
}

start_client()
{
	s=$1
	cmd=${2:-cat}

	cleanup_servers
	$TERMO new-session -d -s "$s" -n main -x 80 -y 24 "$cmd" || \
		fail "$s: new-session failed"
	$TERMO2 new-session -d -s out -x 80 -y 24 "$TERMO attach -t $s" || \
		fail "$s: outer client failed"
	wait_clients 1
}

new_outer_client()
{
	s=$1

	$TERMO2 new-window -d -t out: "$TERMO attach -t $s" || \
		fail "$s: outer client failed"
}

client_for_session()
{
	$TERMO list-clients -F '#{client_name} #{client_session}' |
	    awk -v s="$1" '$2 == s { print $1; exit }'
}

test_choose_tree()
{
	start_client tree-a
	$TERMO new-session -d -s tree-b -n one 'cat' || fail "tree-b failed"
	$TERMO new-window -d -t tree-b -n two 'cat' || fail "tree-b:1 failed"
	$TERMO new-session -d -s tree-c -n one 'cat' || fail "tree-c failed"
	$TERMO new-session -d -s tree-d -n one 'cat' || fail "tree-d failed"
	$TERMO split-window -d -t tree-a:0 'cat' || fail "tree split failed"

	$TERMO choose-tree -t tree-a:0 -O index -F 'MT #{session_name}:#{window_index}.#{pane_index}' || \
		fail "choose-tree failed"
	wait_for 'MT '
	repeat_key tree-a:0 j 40

	$TERMO kill-session -t tree-d || fail "tree kill-session failed"
	$TERMO kill-session -t tree-c || fail "tree kill-session failed"
	$TERMO rename-session -t tree-b tree-renamed || fail "tree rename failed"
	$TERMO rename-window -t tree-renamed:0 renamed || fail "tree rename-window failed"
	$TERMO kill-window -t tree-renamed:1 || fail "tree kill-window failed"
	side=$($TERMO split-window -d -P -F '#{pane_id}' -t tree-a:0 'cat') || \
		fail "tree side split failed"
	$TERMO break-pane -d -s "$side" || fail "tree break-pane failed"
	$TERMO join-pane -d -s "$side" -t tree-a:0.0 || \
		fail "tree join-pane failed"
	i=0
	while [ "$i" -lt 12 ]; do
		$TERMO new-window -d -t tree-a -n "new$i" 'cat' || \
			fail "tree new-window failed"
		i=$((i + 1))
	done

	assert_alive "choose-tree mutation"
	$TERMO send-keys -t tree-a:0 k j l h Enter || \
		fail "choose-tree keys failed"
	wait_mode tree-a:0 0
	assert_alive "choose-tree exit"
}

test_choose_buffer()
{
	start_client buffer-a

	i=0
	while [ "$i" -lt 30 ]; do
		$TERMO set-buffer -b "mbuf$i" "buffer mutation $i" || \
			fail "set-buffer failed"
		i=$((i + 1))
	done

	$TERMO choose-buffer -t buffer-a:0 -F 'MB #{buffer_name}' || \
		fail "choose-buffer failed"
	wait_for 'MB '
	repeat_key buffer-a:0 j 40

	i=8
	while [ "$i" -lt 30 ]; do
		$TERMO delete-buffer -b "mbuf$i" || fail "delete-buffer failed"
		i=$((i + 1))
	done
	i=30
	while [ "$i" -lt 50 ]; do
		$TERMO set-buffer -b "mbuf$i" "new buffer mutation $i" || \
			fail "new set-buffer failed"
		i=$((i + 1))
	done

	assert_alive "choose-buffer mutation"
	$TERMO send-keys -t buffer-a:0 k j Enter || \
		fail "choose-buffer keys failed"
	wait_mode buffer-a:0 0
	assert_alive "choose-buffer exit"
}

test_choose_client()
{
	start_client client-a
	$TERMO new-session -d -s client-b -n main 'cat' || fail "client-b failed"
	$TERMO new-session -d -s client-c -n main 'cat' || fail "client-c failed"
	new_outer_client client-b
	new_outer_client client-c
	wait_clients 3

	$TERMO choose-client -t client-a:0 -F 'MC #{client_session}' || \
		fail "choose-client failed"
	wait_for 'MC '
	repeat_key client-a:0 j 20

	c=$(client_for_session client-c)
	[ -n "$c" ] || fail "client-c client not found"
	$TERMO detach-client -t "$c" || fail "detach client-c failed"
	c=$(client_for_session client-b)
	[ -n "$c" ] || fail "client-b client not found"
	$TERMO detach-client -t "$c" || fail "detach client-b failed"

	$TERMO new-session -d -s client-d -n main 'cat' || fail "client-d failed"
	new_outer_client client-d
	wait_clients 2

	assert_alive "choose-client mutation"
	$TERMO send-keys -t client-a:0 k j Enter || \
		fail "choose-client keys failed"
	wait_mode client-a:0 0
	assert_alive "choose-client exit"
}

test_customize_mode()
{
	start_client option-a

	i=0
	while [ "$i" -lt 30 ]; do
		$TERMO set-option -g "@mode_mut_$i" "$i" || \
			fail "set option failed"
		i=$((i + 1))
	done

	$TERMO customize-mode -t option-a:0 -F 'MO #{option_name}=#{option_value}' || \
		fail "customize-mode failed"
	wait_mode option-a:0 1
	repeat_key option-a:0 j 80

	i=10
	while [ "$i" -lt 30 ]; do
		$TERMO set-option -gu "@mode_mut_$i" || fail "unset option failed"
		i=$((i + 1))
	done
	i=30
	while [ "$i" -lt 55 ]; do
		$TERMO set-option -g "@mode_mut_$i" "$i" || \
			fail "new option failed"
		i=$((i + 1))
	done
	$TERMO set-option -g status-left 'mutated' || fail "status-left failed"
	$TERMO rename-session -t option-a option-renamed || fail "option rename failed"

	assert_alive "customize-mode mutation"
	$TERMO send-keys -t option-renamed:0 k j C-d C-u q || \
		fail "customize-mode keys failed"
	wait_mode option-renamed:0 0
	assert_alive "customize-mode exit"
}

test_customize_break_pane()
{
	start_client option-break
	side=$($TERMO split-window -d -P -F '#{pane_id}' \
	    -t option-break:0 'cat') || fail "customize split failed"

	$TERMO customize-mode -t "$side" || fail "customize-mode failed"
	wait_mode "$side" 1
	$TERMO break-pane -d -s "$side" || fail "customize break-pane failed"

	assert_alive "customize-mode break-pane"
	wait_mode "$side" 1
	$TERMO send-keys -t "$side" q || fail "customize-mode quit failed"
	wait_mode "$side" 0
}

test_copy_mode()
{
	start_client copy-a 'i=0; while [ $i -lt 200 ]; do echo "copy mutation line $i"; i=$((i + 1)); done; cat'
	$TERMO set-window-option -g mode-keys vi || fail "mode-keys failed"
	$TERMO split-window -d -t copy-a:0 'cat' || fail "copy split failed"

	$TERMO copy-mode -t copy-a:0 || fail "copy-mode failed"
	wait_mode copy-a:0 1
	repeat_key copy-a:0 k 20

	$TERMO rename-window -t copy-a:0 renamed || fail "copy rename-window failed"
	side=$($TERMO split-window -d -P -F '#{pane_id}' -t copy-a:renamed 'cat') || \
		fail "copy side split failed"
	$TERMO break-pane -d -s "$side" || fail "copy break-pane failed"
	$TERMO join-pane -d -s "$side" -t copy-a:renamed.0 || \
		fail "copy join-pane failed"
	$TERMO kill-pane -t copy-a:renamed.1 || fail "copy kill-pane failed"
	$TERMO new-window -d -t copy-a -n extra 'cat' || fail "copy new-window failed"
	$TERMO kill-window -t copy-a:extra || fail "copy kill-window failed"
	$TERMO rename-session -t copy-a copy-renamed || fail "copy rename failed"

	assert_alive "copy-mode mutation"
	$TERMO send-keys -t copy-renamed:renamed.0 j k C-d C-u q || \
		fail "copy-mode keys failed"
	wait_mode copy-renamed:renamed.0 0
	assert_alive "copy-mode exit"
}

cleanup_servers
test_choose_tree
test_choose_buffer
test_choose_client
test_customize_mode
test_customize_break_pane
test_copy_mode
cleanup
exit 0
