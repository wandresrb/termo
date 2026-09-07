PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"

TMP=$(mktemp)
EXP=$(mktemp)
trap 'rm -f "$TMP" "$EXP"; $TERMO kill-server 2>/dev/null' 0 1 15

exit_status=0

fail()
{
	echo "FAIL: $1"
	diff -u "$EXP" "$TMP"
	exit_status=1
}

start_pane()
{
	start_pane_hlimit "$1" "$2" "$3" "$4" 0
}

start_pane_history()
{
	start_pane_hlimit "$1" "$2" "$3" "$4" 2000
}

start_pane_hlimit()
{
	name=$1
	sx=$2
	sy=$3
	seq=$4
	hlimit=$5

	$TERMO kill-server 2>/dev/null
	sleep 0.1
	$TERMO new-session -d -x 1 -y 1 -s test-setup "sleep 2" || exit 1
	$TERMO set-option -g history-limit "$hlimit" || exit 1
	$TERMO new-session -d -x "$sx" -y "$sy" -s "$name" \
	    "printf '$seq'; sleep 2" || exit 1
	$TERMO kill-session -t test-setup
	sleep 0.3
}

start_cmd()
{
	name=$1
	sx=$2
	sy=$3
	cmd=$4

	$TERMO kill-server 2>/dev/null
	sleep 0.1
	$TERMO new-session -d -x "$sx" -y "$sy" -s "$name" "$cmd" || exit 1
	sleep 0.3
}

normalize_capture()
{
	sed 's/[	 ]*$//' |
	    awk '{ line[NR] = $0; if ($0 != "") last = NR }
	        END { for (i = 1; i <= last; i++) print line[i] }'
}

capture_grid()
{
	$TERMO capture-pane -pN -t "$1:" -S 0 -E - | normalize_capture
}

# Pane output arrives asynchronously; poll instead of trusting a fixed sleep.
wait_until()
{
	i=0
	until "$@"; do
		i=$((i + 1))
		[ "$i" -ge 50 ] && return 1
		sleep 0.1
	done
}

capture_matches()
{
	capture_grid "$1" >"$TMP"
	cmp -s "$TMP" "$EXP"
}

check_capture()
{
	name=$1
	expected=$2
	printf "%s\n" "$expected" >"$EXP"
	wait_until capture_matches "$name" || fail "$name"
}

cursor_matches()
{
	$TERMO display-message -p -t "$1:" '#{cursor_x},#{cursor_y}' >"$TMP"
	cmp -s "$TMP" "$EXP"
}

check_cursor()
{
	name=$1
	expected=$2
	printf "%s\n" "$expected" >"$EXP"
	wait_until cursor_matches "$name" || fail "$name cursor"
}

check_flags()
{
	name=$1
	expected=$2

	$TERMO capture-pane -pNF -t "$name:" -S 0 -E - |
	    normalize_capture |
	    awk '$0 != "-"' >"$TMP"
	printf "%s\n" "$expected" >"$EXP"
	cmp "$TMP" "$EXP" || fail "$name flags"
}

check_joined()
{
	name=$1
	expected=$2

	$TERMO capture-pane -pNJ -t "$name:" -S 0 -E - |
	    normalize_capture >"$TMP"
	printf "%s\n" "$expected" >"$EXP"
	cmp "$TMP" "$EXP" || fail "$name joined"
}

capture_raw()
{
	$TERMO capture-pane -pR -t "$1:"
}

capture_raw_used()
{
	capture_raw "$1" |
	    awk '/^(G|	L)/ || /^		C/ && $3 !~ /^data=\(1,1, \)$/'
}

check_raw()
{
	name=$1
	expected=$2

	capture_raw "$name" >"$TMP"
	printf "%s\n" "$expected" >"$EXP"
	cmp "$TMP" "$EXP" || fail "$name raw"
}

check_raw_used()
{
	name=$1
	expected=$2

	capture_raw_used "$name" >"$TMP"
	printf "%s\n" "$expected" >"$EXP"
	cmp "$TMP" "$EXP" || fail "$name raw used"
}

check_raw_has()
{
	name=$1
	shift

	capture_raw "$name" >"$TMP"
	for expected in "$@"; do
		if ! grep -Fqx "$expected" "$TMP"; then
			printf "%s\n" "$expected" >"$EXP"
			fail "$name raw missing"
		fi
	done
}

raw_matches_all()
{
	name=$1
	shift
	capture_raw "$name" >"$TMP"
	for expected in "$@"; do
		grep -Eq "$expected" "$TMP" || return 1
	done
}

check_raw_matches()
{
	name=$1
	shift

	wait_until raw_matches_all "$name" "$@" && return
	for expected in "$@"; do
		if ! grep -Eq "$expected" "$TMP"; then
			printf "%s\n" "$expected" >"$EXP"
			fail "$name raw missing"
		fi
	done
}

check_raw_no_matches()
{
	name=$1
	shift

	capture_raw "$name" >"$TMP"
	for unexpected in "$@"; do
		if grep -Eq "$unexpected" "$TMP"; then
			printf "%s\n" "$unexpected" >"$EXP"
			fail "$name raw unexpected"
		fi
	done
}
