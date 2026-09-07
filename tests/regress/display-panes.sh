#!/bin/sh

# Tests for display-panes as panes-mode.

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

cleanup()
{
	$TERMO kill-server 2>/dev/null
	$TERMO2 kill-server 2>/dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT

fail()
{
	echo "$1" >&2
	exit 1
}

wait_format()
{
	target=$1
	format=$2
	want=$3

	i=0
	while [ "$i" -lt 50 ]; do
		got=$($TERMO display-message -p -t "$target" "$format" 2>/dev/null)
		[ "$got" = "$want" ] && return 0
		sleep 0.2
		i=$((i + 1))
	done
	fail "$target: expected '$want' for $format, got '$got'"
}

wait_option()
{
	option=$1
	want=$2

	i=0
	while [ "$i" -lt 50 ]; do
		got=$($TERMO show -gv "$option" 2>/dev/null)
		[ "$got" = "$want" ] && return 0
		sleep 0.2
		i=$((i + 1))
	done
	fail "$option: expected '$want', got '$got'"
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

$TERMO new-session -d -s m -x 80 -y 24 'cat' || exit 1
$TERMO split-window -t m:0 'cat' || exit 1
p0=$($TERMO display-message -p -t m:0.0 '#{pane_id}')
p1=$($TERMO display-message -p -t m:0.1 '#{pane_id}')
$TERMO set -g display-panes-format \
    'P#{pane_index}:#{pane_unzoomed_width}x#{pane_unzoomed_height}' ||
	fail "set display-panes-format failed"

$TERMO2 new-session -d -s out -x 80 -y 24 "$TERMO attach -t m" || exit 1
wait_clients 1
p0_size=$($TERMO display-message -p -t "$p0" '#{pane_width}x#{pane_height}')
p1_size=$($TERMO display-message -p -t "$p1" '#{pane_width}x#{pane_height}')

capture()
{
	$TERMO2 capture-pane -p -t out:0 2>/dev/null
}

wait_capture()
{
	i=0
	while [ "$i" -lt 50 ]; do
		CAPTURED=$(capture)
		printf '%s\n' "$CAPTURED" | grep -F -q "$1" && return 0
		sleep 0.2
		i=$((i + 1))
	done
	fail "timed out waiting for '$1' in capture"
}

# Entry, default zoom, and exit.
$TERMO display-panes -d 0 -t "$p0" || fail "display-panes failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
wait_format "$p0" '#{window_zoomed_flag}' '1'
wait_format "$p0" '#{pane_unzoomed_width}x#{pane_unzoomed_height}' \
    "$p0_size"
wait_capture "P0:$p0_size"
wait_capture "P1:$p1_size"
$TERMO send-keys -t "$p0" q || fail "exit panes mode failed"
wait_format "$p0" '#{pane_in_mode}' '0'
wait_format "$p0" '#{window_zoomed_flag}' '0'
$TERMO set -g display-panes-format 'P#{pane_index}' ||
	fail "reset display-panes-format failed"

# Unzoomed sizes match the visible pane size when scrollbars reserve space.
$TERMO set -w -t m:0 pane-scrollbars on ||
	fail "set pane-scrollbars failed"
$TERMO set -w -t m:0 pane-scrollbars-style "width=2,pad=1" ||
	fail "set pane-scrollbars-style failed"
p0_size=$($TERMO display-message -p -t "$p0" '#{pane_width}x#{pane_height}')
p1_size=$($TERMO display-message -p -t "$p1" '#{pane_width}x#{pane_height}')
$TERMO set -g display-panes-format \
    'S#{pane_index}:#{pane_unzoomed_width}x#{pane_unzoomed_height}' ||
	fail "set scrollbar display-panes-format failed"
$TERMO display-panes -d 0 -t "$p0" || fail "display-panes scrollbar failed"
wait_format "$p0" '#{window_zoomed_flag}' '1'
wait_format "$p0" '#{pane_unzoomed_width}x#{pane_unzoomed_height}' \
    "$p0_size"
wait_capture "S0:$p0_size"
wait_capture "S1:$p1_size"
$TERMO send-keys -t "$p0" q || fail "exit scrollbar panes mode failed"
wait_format "$p0" '#{pane_in_mode}' '0'
$TERMO set -w -t m:0 pane-scrollbars off ||
	fail "reset pane-scrollbars failed"
$TERMO set -g display-panes-format 'P#{pane_index}' ||
	fail "reset display-panes-format after scrollbars failed"

# -Z starts unzoomed.
$TERMO display-panes -Zd 0 -t "$p0" || fail "display-panes -Z failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
wait_format "$p0" '#{window_zoomed_flag}' '0'
$TERMO send-keys -t "$p0" q || fail "exit unzoomed panes mode failed"
wait_format "$p0" '#{pane_in_mode}' '0'

# Selection keys run the command template and close the mode.
$TERMO set -g @picked none || fail "set @picked failed"
$TERMO display-panes -d 0 -t "$p0" 'set -g @picked %%' ||
	fail "display-panes selection failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
$TERMO send-keys -t "$p0" 1 || fail "select pane failed"
wait_format "$p0" '#{pane_in_mode}' '0'
wait_option @picked "$p1"

# Commands after display-panes run immediately while the mode remains.
$TERMO set -g @after none || fail "set @after failed"
$TERMO display-panes -Nd 500 -t "$p0" \; set -g @after fast ||
	fail "display-panes immediate command failed"
wait_option @after fast
wait_format "$p0" '#{pane_mode}' 'panes-mode'
wait_format "$p0" '#{pane_in_mode}' '0'

# Existing zoom is restored on exit.
$TERMO select-pane -t "$p0" || fail "select p0 failed"
$TERMO resize-pane -Z -t "$p0" || fail "zoom failed"
wait_format "$p0" '#{window_zoomed_flag}' '1'
$TERMO display-panes -d 0 -t "$p0" || fail "display-panes zoomed failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
$TERMO send-keys -t "$p0" q || fail "exit zoomed panes mode failed"
wait_format "$p0" '#{pane_in_mode}' '0'
wait_format "$p0" '#{window_zoomed_flag}' '1'
$TERMO resize-pane -Z -t "$p0" || fail "unzoom cleanup failed"

# Panes mode can be stacked above another mode and returns to it on exit.
$TERMO copy-mode -t "$p0" || fail "copy-mode failed"
wait_format "$p0" '#{pane_mode}' 'copy-mode'
$TERMO display-panes -d 0 -t "$p0" 'set -g @picked %%' ||
	fail "display-panes over copy-mode failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
$TERMO send-keys -t "$p0" 1 || fail "select from stacked mode failed"
wait_format "$p0" '#{pane_mode}' 'copy-mode'
wait_option @picked "$p1"
$TERMO send-keys -t "$p0" -X cancel || fail "copy-mode cancel failed"
wait_format "$p0" '#{pane_in_mode}' '0'

# Panes mode is not kept underneath another mode.
$TERMO display-panes -d 0 -t "$p0" || fail "display-panes no-stack failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
$TERMO copy-mode -t "$p0" || fail "copy-mode over panes-mode failed"
wait_format "$p0" '#{pane_mode}' 'copy-mode'
$TERMO send-keys -t "$p0" -X cancel || fail "copy-mode cancel failed"
wait_format "$p0" '#{pane_in_mode}' '0'
wait_format "$p0" '#{window_zoomed_flag}' '0'

# -s shows panes from a different source window but enters the mode in the
# target pane.
$TERMO new-window -d -t m: -n src 'cat' || fail "new source window failed"
$TERMO split-window -t m:src 'cat' || fail "split source window failed"
sp0=$($TERMO display-message -p -t m:src.0 '#{pane_id}')
sp1=$($TERMO display-message -p -t m:src.1 '#{pane_id}')
$TERMO respawn-pane -k -t "$sp0" 'printf "SOURCE-PANE\n"; exec cat' ||
	fail "write source marker failed"
$TERMO set -g display-panes-format '' || fail "clear source display format failed"
$TERMO set -w -t m:src display-panes-format 'SOURCE-ONLY' ||
	fail "set source window display format failed"
$TERMO set -g @picked none || fail "reset @picked failed"
$TERMO display-panes -d 0 -s m:src -t "$p0" 'set -g @picked %%' ||
	fail "display-panes source window failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
wait_capture 'SOURCE-PANE'
CAPTURED=$(capture)
printf '%s\n' "$CAPTURED" | grep -F -q 'SOURCE-ONLY' &&
	fail "used source window display-panes-format"
$TERMO send-keys -t "$p0" 1 || fail "select source pane failed"
wait_format "$p0" '#{pane_in_mode}' '0'
wait_option @picked "$sp1"
$TERMO kill-window -t m:src || fail "kill source window failed"
$TERMO set -g display-panes-format 'P#{pane_index}' ||
	fail "reset display-panes-format after source failed"

# pane-border-status rows are drawn as plain preview borders and do not shift
# pane content into the status row.
$TERMO set -w -t m:0 pane-border-status top ||
	fail "set pane-border-status failed"
status_p0_size=$($TERMO display-message -p -t "$p0" \
    '#{pane_width}x#{pane_height}')
status_p1_size=$($TERMO display-message -p -t "$p1" \
    '#{pane_width}x#{pane_height}')
$TERMO set -g display-panes-format \
    '#[align=right]T#{pane_index}:#{pane_unzoomed_width}x#{pane_unzoomed_height}' ||
	fail "set status display-panes-format failed"
$TERMO respawn-pane -k -t "$p0" 'printf "STATUS-TOP\n"; exec cat' ||
	fail "write status marker failed"
wait_capture 'STATUS-TOP'
$TERMO display-panes -d 0 -t "$p0" || fail "display-panes status failed"
wait_format "$p0" '#{pane_mode}' 'panes-mode'
wait_capture 'STATUS-TOP'
wait_capture "T0:$status_p0_size"
wait_capture "T1:$status_p1_size"
CAPTURED=$(capture)
first=$(printf '%s\n' "$CAPTURED" | sed -n '1p')
second=$(printf '%s\n' "$CAPTURED" | sed -n '2p')
printf '%s\n' "$first" | grep -F -q 'STATUS-TOP' &&
	fail "pane content drawn in status border row"
printf '%s\n' "$second" | grep -F -q 'STATUS-TOP' ||
	fail "pane content not drawn below status border row"
$TERMO send-keys -t "$p0" q || fail "exit status panes mode failed"
wait_format "$p0" '#{pane_in_mode}' '0'
$TERMO set -w -t m:0 pane-border-status off ||
	fail "reset pane-border-status failed"
$TERMO set -g display-panes-format 'P#{pane_index}' ||
	fail "reset display-panes-format failed"

# Floating panes are always framed in panes-mode, independent of their real
# pane-border-lines setting.
fp=$($TERMO new-pane -dPF '#{pane_id}' -B none -x 30 -y 8 -X 8 -Y 3 \
    -t m:0 'cat') || fail "new borderless floating pane failed"
$TERMO set -g display-panes-format '' || fail "clear display-panes-format failed"
$TERMO display-panes -Zd 0 -t "$fp" || fail "display-panes floating failed"
wait_format "$fp" '#{pane_mode}' 'panes-mode'
wait_capture '┌'
wait_capture '┘'
$TERMO send-keys -t "$fp" q || fail "exit floating panes mode failed"
wait_format "$fp" '#{pane_in_mode}' '0'

$TERMO display-panes -d 0 -t "$fp" || fail "display-panes floating zoomed failed"
wait_format "$fp" '#{pane_mode}' 'panes-mode'
wait_format "$fp" '#{window_zoomed_flag}' '1'
wait_capture '┌'
wait_capture '┘'
fp_size=$($TERMO display-message -p -t "$fp" \
    '#{pane_unzoomed_width}x#{pane_unzoomed_height}')
[ "$fp_size" = "30x8" ] ||
	fail "expected floating unzoomed size 30x8, got $fp_size"
$TERMO send-keys -t "$fp" q || fail "exit zoomed floating panes mode failed"
wait_format "$fp" '#{pane_in_mode}' '0'
wait_format "$fp" '#{window_zoomed_flag}' '0'

$TERMO resize-pane -t "$fp" -x 48 -y 14 ||
	fail "resize floating pane larger failed"
$TERMO set -g display-panes-format \
    '#[align=right]FP#{pane_unzoomed_width}x#{pane_unzoomed_height}' ||
	fail "set floating display-panes-format failed"
$TERMO display-panes -Zd 0 -t "$fp" || fail "display-panes floating format failed"
wait_format "$fp" '#{pane_mode}' 'panes-mode'
wait_capture 'FP48x14'
$TERMO resize-pane -t "$fp" -x 30 -y 10 ||
	fail "resize floating pane smaller failed"
wait_capture 'FP30x10'
$TERMO send-keys -t "$fp" q || fail "exit floating format panes mode failed"
wait_format "$fp" '#{pane_in_mode}' '0'

$TERMO kill-pane -t "$fp" || fail "kill floating pane failed"
$TERMO set -g display-panes-format 'P#{pane_index}' ||
	fail "reset display-panes-format after floating failed"

exit 0
