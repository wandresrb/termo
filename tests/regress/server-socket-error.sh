#!/bin/sh

# Server socket creation failures must produce a nonzero client exit status.

PATH=/bin:/usr/bin
TERM=screen
export TERM

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"

# Root bypasses directory mode bits, so it cannot trigger this failure.
[ "$(id -u)" -eq 0 ] && exit 0

TMP=$(mktemp -d) || exit 1
SOCKET="$TMP/ro/socket"

cleanup()
{
	chmod 700 "$TMP/ro" 2>/dev/null
	rm -rf "$TMP"
}

fail()
{
	echo "$1" >&2
	cleanup
	exit 1
}

mkdir "$TMP/ro" || exit 1
chmod 500 "$TMP/ro" || exit 1
trap cleanup 0 1 15

check_failure()
{
	output=$($TEST_TERMO -S "$SOCKET" -f/dev/null "$@" 2>&1)
	retval=$?

	[ "$retval" -ne 0 ] || fail "tmux $* exited zero: $output"
	case "$output" in
	"error creating $SOCKET ("*) ;;
	*) fail "tmux $* produced unexpected error: $output" ;;
	esac
}

check_failure start-server
check_failure new-session -d

exit 0
