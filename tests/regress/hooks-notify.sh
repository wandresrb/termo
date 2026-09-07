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
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"

fail()
{
	echo "$*" >&2
	$TERMO kill-server 2>/dev/null || true
	$TERMO2 kill-server 2>/dev/null || true
	rm -rf "$OUT"
	exit 1
}

cleanup()
{
	$TERMO kill-server 2>/dev/null || true
	$TERMO2 kill-server 2>/dev/null || true
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

$TERMO new -d -s main || fail "new-session main failed"

# session-created, session-renamed, session-closed.
$TERMO set -g @s 0 || fail "set @s failed"
$TERMO set-hook -g session-created \
	'set -gF @s "#{hook}:#{hook_session_name}"' ||
	fail "set-hook session-created failed"
$TERMO new -d -s tmp || fail "new-session tmp failed"
wait_for @s 'session-created:tmp'
$TERMO set-hook -gu session-created || fail "unset session-created failed"

$TERMO set-hook -g session-renamed \
	'set -gF @s "#{hook}:#{hook_session_name}"' ||
	fail "set-hook session-renamed failed"
$TERMO rename-session -t tmp tmp2 || fail "rename-session failed"
wait_for @s 'session-renamed:tmp2'
$TERMO set-hook -gu session-renamed || fail "unset session-renamed failed"

$TERMO set-hook -g session-closed \
	'set -gF @s "#{hook}:#{hook_session_name}"' ||
	fail "set-hook session-closed failed"
$TERMO kill-session -t tmp2 || fail "kill-session failed"
wait_for @s 'session-closed:tmp2'
$TERMO set-hook -gu session-closed || fail "unset session-closed failed"

# window-linked and window-unlinked from new-window and kill-window.
$TERMO set -g @w 0 || fail "set @w failed"
$TERMO set-hook -g window-linked \
	'set -gF @w "#{hook}:#{hook_session_name}:#{hook_window_name}"' ||
	fail "set-hook window-linked failed"
$TERMO set-hook -g window-unlinked \
	'set -gF @w "#{hook}:#{hook_session_name}:#{hook_window_name}"' ||
	fail "set-hook window-unlinked failed"
$TERMO neww -d -t main: -n mywin || fail "new-window failed"
wait_for @w 'window-linked:main:mywin'

# window-linked and window-unlinked from link-window and unlink-window.
$TERMO new -d -s other || fail "new-session other failed"
$TERMO set -g @w 0 || fail "reset @w failed"
$TERMO link-window -s main:mywin -t other:5 || fail "link-window failed"
wait_for @w 'window-linked:other:mywin'
$TERMO set -g @w 0 || fail "reset @w failed"
$TERMO unlink-window -t other:5 || fail "unlink-window failed"
wait_for @w 'window-unlinked:other:mywin'
$TERMO set -g @w 0 || fail "reset @w failed"
$TERMO kill-window -t main:mywin || fail "kill-window failed"
wait_for @w 'window-unlinked:main:mywin'
$TERMO set-hook -gu window-linked || fail "unset window-linked failed"
$TERMO set-hook -gu window-unlinked || fail "unset window-unlinked failed"

# window-created and window-closed are actual window lifetime hooks.
$TERMO set -g @wc 0 || fail "set @wc failed"
$TERMO set-hook -g window-created \
	'set -gF @wc "#{@wc}|#{hook}:#{hook_window}:#{hook_window_name}"' ||
	fail "set-hook window-created failed"
$TERMO set-hook -g window-closed \
	'set -gF @wc "#{@wc}|#{hook}:#{hook_window}:#{hook_window_name}"' ||
	fail "set-hook window-closed failed"
$TERMO neww -d -t main: -n lifetime || fail "new-window lifetime failed"
window=$($TERMO display -pt main:lifetime '#{window_id}') ||
	fail "display lifetime window failed"
wait_for @wc "0|window-created:$window:lifetime"
$TERMO kill-window -t main:lifetime || fail "kill-window lifetime failed"
wait_for @wc "0|window-created:$window:lifetime|window-closed:$window:lifetime"
$TERMO set-hook -gu window-created || fail "unset window-created failed"
$TERMO set-hook -gu window-closed || fail "unset window-closed failed"

# window-renamed sees the window id.
window=$($TERMO display -pt main:0 '#{window_id}') ||
	fail "display window_id failed"
$TERMO set -g @r 0 || fail "set @r failed"
$TERMO set-hook -g window-renamed \
	'set -gF @r "#{hook}:#{hook_window}:#{hook_window_name}"' ||
	fail "set-hook window-renamed failed"
$TERMO rename-window -t main:0 renamed || fail "rename-window failed"
wait_for @r "window-renamed:$window:renamed"
$TERMO set-hook -gu window-renamed || fail "unset window-renamed failed"

# window-layout-changed from split-window.
$TERMO set -g @l 0 || fail "set @l failed"
$TERMO set-hook -g window-layout-changed \
	'set -gF @l "#{hook}:#{hook_window}"' ||
	fail "set-hook window-layout-changed failed"
$TERMO splitw -d -t main:0 || fail "split-window failed"
wait_for @l "window-layout-changed:$window"
$TERMO set-hook -gu window-layout-changed ||
	fail "unset window-layout-changed failed"

# window-pane-changed from select-pane.
old_pane=$($TERMO display -pt main:0.0 '#{pane_id}') ||
	fail "display old pane failed"
new_pane=$($TERMO display -pt main:0.1 '#{pane_id}') ||
	fail "display new pane failed"
$TERMO set -g @p 0 || fail "set @p failed"
$TERMO set-hook -g window-pane-changed \
	'set -gF @p "#{hook}:#{hook_window}:#{hook_pane}:#{hook_old_pane}:#{hook_new_pane}"' ||
	fail "set-hook window-pane-changed failed"
$TERMO selectp -t main:0.1 || fail "select-pane failed"
wait_for @p "window-pane-changed:$window:$new_pane:$old_pane:$new_pane"

# window-pane-changed from removing the active pane.
$TERMO set -g @p 0 || fail "reset @p failed"
$TERMO killp -t "$new_pane" || fail "kill active pane failed"
wait_for @p "window-pane-changed:$window:$old_pane:$new_pane:$old_pane"
$TERMO set-hook -gu window-pane-changed ||
	fail "unset window-pane-changed failed"

# session-window-changed from select-window.
$TERMO neww -d -t main: -n w2 || fail "new-window w2 failed"
old_window=$($TERMO display -pt main:0 '#{window_id}') ||
	fail "display old window failed"
new_window=$($TERMO display -pt main:w2 '#{window_id}') ||
	fail "display new window failed"
old_index=$($TERMO display -pt main:0 '#{window_index}') ||
	fail "display old window index failed"
new_index=$($TERMO display -pt main:w2 '#{window_index}') ||
	fail "display new window index failed"
$TERMO set -g @c 0 || fail "set @c failed"
$TERMO set-hook -g session-window-changed \
	'set -gF @c "#{hook}:#{hook_session_name}:#{hook_window}:#{hook_old_window}:#{hook_new_window}:#{hook_old_window_index}->#{hook_new_window_index}"' ||
	fail "set-hook session-window-changed failed"
$TERMO selectw -t main:w2 || fail "select-window failed"
wait_for @c "session-window-changed:main:$new_window:$old_window:$new_window:$old_index->$new_index"
$TERMO set-hook -gu session-window-changed ||
	fail "unset session-window-changed failed"

# session-added-to-group and session-removed-from-group carry group details.
$TERMO set -g @sg 0 || fail "set @sg failed"
$TERMO set-hook -g session-added-to-group \
	'set -gF @sg "#{@sg}|#{hook}:#{hook_session_name}:#{hook_group}:#{hook_group_size}"' ||
	fail "set-hook session-added-to-group failed"
$TERMO set-hook -g session-removed-from-group \
	'set -gF @sg "#{@sg}|#{hook}:#{hook_session_name}:#{hook_group}:#{hook_group_size}"' ||
	fail "set-hook session-removed-from-group failed"
$TERMO new -d -s group-a || fail "new-session group-a failed"
$TERMO new -d -s group-b -t group-a || fail "new-session group-b failed"
wait_for @sg "0|session-added-to-group:group-a:group-a:1|session-added-to-group:group-b:group-a:2"
$TERMO kill-session -t group-b || fail "kill-session group-b failed"
wait_for @sg "0|session-added-to-group:group-a:group-a:1|session-added-to-group:group-b:group-a:2|session-removed-from-group:group-b:group-a:2"
$TERMO set-hook -gu session-added-to-group ||
	fail "unset session-added-to-group failed"
$TERMO set-hook -gu session-removed-from-group ||
	fail "unset session-removed-from-group failed"
$TERMO kill-session -t group-a || fail "kill-session group-a failed"

# window-resized carries old and new dimensions.
old_size=$($TERMO display -pt main:0 '#{window_width},#{window_height}') ||
	fail "display old window size failed"
$TERMO set -g @wr 0 || fail "set @wr failed"
$TERMO set-hook -g window-resized \
	'set -gF @wr "#{hook}:#{hook_window}:#{hook_old_width},#{hook_old_height}->#{hook_width},#{hook_height}"' ||
	fail "set-hook window-resized failed"
$TERMO resizew -t main:0 -x 70 -y 20 || fail "resize-window failed"
new_size=$($TERMO display -pt main:0 '#{window_width},#{window_height}') ||
	fail "display new window size failed"
wait_for @wr "window-resized:$old_window:$old_size->$new_size"
$TERMO set-hook -gu window-resized || fail "unset window-resized failed"

# pane-mode-changed from entering and leaving copy mode.
$TERMO set -g @m 0 || fail "set @m failed"
pane=$($TERMO display -pt main:0.0 '#{pane_id}') ||
	fail "display pane_id failed"
$TERMO set-hook -g pane-mode-changed \
	'set -gF @m "#{hook}:#{hook_pane}:#{pane_in_mode}"' ||
	fail "set-hook pane-mode-changed failed"
$TERMO copy-mode -t main:0.0 || fail "copy-mode failed"
wait_for @m "pane-mode-changed:$pane:1"
$TERMO send-keys -t main:0.0 -X cancel || fail "cancel failed"
wait_for @m "pane-mode-changed:$pane:0"
$TERMO set-hook -gu pane-mode-changed || fail "unset pane-mode-changed failed"

# pane-exited when a pane's command exits with remain-on-exit off.
$TERMO set -g @x 0 || fail "set @x failed"
$TERMO set-hook -g pane-exited 'set -gF @x "#{hook}:#{hook_pane}"' ||
	fail "set-hook pane-exited failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' 'true') ||
	fail "split-window true failed"
wait_for @x "pane-exited:$pane"
$TERMO set-hook -gu pane-exited || fail "unset pane-exited failed"

# pane-died when a pane's command exits with remain-on-exit on.
$TERMO set -g remain-on-exit on || fail "set remain-on-exit failed"
$TERMO set -g @d 0 || fail "set @d failed"
$TERMO set-hook -g pane-died 'set -gF @d "#{hook}:#{hook_pane}"' ||
	fail "set-hook pane-died failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' 'true') ||
	fail "split-window remain-on-exit failed"
wait_for @d "pane-died:$pane"
$TERMO set-hook -gu pane-died || fail "unset pane-died failed"
$TERMO killp -t "$pane" || fail "kill-pane failed"
$TERMO set -g remain-on-exit off || fail "reset remain-on-exit failed"

# pane-title-changed from split-window -T.
$TERMO set -g @t 0 || fail "set @t failed"
$TERMO set-hook -g pane-title-changed \
	'set -gF @t "#{hook}:#{hook_pane}:#{hook_new_title}:#{pane_title}"' ||
	fail "set-hook pane-title-changed failed"
pane=$($TERMO splitw -d -t main:0 -T splittitle -P -F '#{pane_id}' \
	'sleep 30') ||
	fail "split-window title option failed"
wait_for @t "pane-title-changed:$pane:splittitle:splittitle"
$TERMO killp -t "$pane" || fail "kill-pane title option failed"

# pane-title-changed from select-pane -T.
$TERMO set -g @t 0 || fail "reset @t select title failed"
pane=$($TERMO display -pt main:0.0 '#{pane_id}') ||
	fail "display select title pane failed"
$TERMO selectp -t "$pane" -T selecttitle || fail "select-pane title failed"
wait_for @t "pane-title-changed:$pane:selecttitle:selecttitle"

# pane-title-changed when a pane sets its title.
$TERMO set -g @t 0 || fail "reset @t OSC title failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' \
	'printf "\033]2;mytitle\007"; sleep 30') ||
	fail "split-window title failed"
wait_for @t "pane-title-changed:$pane:mytitle:mytitle"
$TERMO killp -t "$pane" || fail "kill-pane title failed"

# pane-title-changed from popping the title stack.
$TERMO set -g @t 0 || fail "reset @t title pop failed"
$TERMO set-hook -g pane-title-changed \
	'set -gF @t "#{@t}|#{hook_new_title}"' ||
	fail "set-hook pane-title-changed pop failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' \
	'printf "\033]2;stackbase\007\033[22;0t\033]2;stacktemp\007\033[23;0t"; sleep 30') ||
	fail "split-window title pop failed"
wait_for @t "0|stackbase|stacktemp|stackbase"
$TERMO set-hook -gu pane-title-changed ||
	fail "unset pane-title-changed failed"
$TERMO killp -t "$pane" || fail "kill-pane title pop failed"

# pane-created payload from a command pane, an empty pane, and a respawn.
$TERMO set -g @pc 0 || fail "set @pc failed"
$TERMO set-hook -g pane-created \
	'set -gF @pc "#{hook}:#{hook_pane}:#{hook_pane_command}:#{hook_created_empty}:#{hook_created_respawn}"' ||
	fail "set-hook pane-created failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' 'sleep 30') ||
	fail "split-window pane-created command failed"
wait_for @pc "pane-created:$pane:\"sleep 30\":0:0"
$TERMO killp -t "$pane" || fail "kill-pane pane-created command failed"

$TERMO set -g @pc 0 || fail "reset @pc failed"
empty_command=$($TERMO show -gqv default-shell) ||
	fail "show default-shell failed"
pane=$($TERMO splitw -d -E -t main:0 -P -F '#{pane_id}') ||
	fail "split-window pane-created empty failed"
wait_for @pc "pane-created:$pane:$empty_command:1:0"

$TERMO set -g @pc 0 || fail "reset @pc respawn failed"
$TERMO respawnp -k -t "$pane" 'sleep 30' || fail "respawn-pane failed"
wait_for @pc "pane-created:$pane:\"sleep 30\":0:1"
$TERMO killp -t "$pane" || fail "kill-pane pane-created respawn failed"
$TERMO set-hook -gu pane-created || fail "unset pane-created failed"

# pane-moved from break-pane and join-pane.
$TERMO neww -d -t main: -n move-a 'sleep 30' ||
	fail "new-window move-a failed"
pane=$($TERMO splitw -d -t main:move-a -P -F '#{pane_id}' 'sleep 30') ||
	fail "split-window pane-moved failed"
target=$($TERMO display -pt main:move-a.0 '#{pane_id}') ||
	fail "display pane-moved target failed"
old_window=$($TERMO display -pt "$pane" '#{window_id}') ||
	fail "display pane-moved old window failed"
old_index=$($TERMO display -pt "$pane" '#{window_index}') ||
	fail "display pane-moved old index failed"
$TERMO set -g @pm 0 || fail "set @pm failed"
$TERMO set-hook -g pane-moved \
	'set -gF @pm "#{hook}:#{hook_pane}:#{hook_old_window}:#{hook_old_window_index}->#{hook_new_window}:#{hook_new_window_index}"' ||
	fail "set-hook pane-moved failed"
$TERMO breakp -d -s "$pane" -n move-b -t main: ||
	fail "break-pane pane-moved failed"
new_window=$($TERMO display -pt "$pane" '#{window_id}') ||
	fail "display pane-moved break new window failed"
new_index=$($TERMO display -pt "$pane" '#{window_index}') ||
	fail "display pane-moved break new index failed"
wait_for @pm "pane-moved:$pane:$old_window:$old_index->$new_window:$new_index"

$TERMO set -g @pm 0 || fail "reset @pm join failed"
old_window=$new_window
old_index=$new_index
$TERMO joinp -d -s "$pane" -t "$target" ||
	fail "join-pane pane-moved failed"
new_window=$($TERMO display -pt "$pane" '#{window_id}') ||
	fail "display pane-moved join new window failed"
new_index=$($TERMO display -pt "$pane" '#{window_index}') ||
	fail "display pane-moved join new index failed"
wait_for @pm "pane-moved:$pane:$old_window:$old_index->$new_window:$new_index"
$TERMO kill-window -t main:move-a || fail "kill-window move-a failed"

# pane-moved from cross-window swap-pane fires once for each pane.
$TERMO neww -d -t main: -n swap-a 'sleep 30' ||
	fail "new-window swap-a failed"
$TERMO neww -d -t main: -n swap-b 'sleep 30' ||
	fail "new-window swap-b failed"
pane1=$($TERMO display -pt main:swap-a.0 '#{pane_id}') ||
	fail "display swap pane1 failed"
pane2=$($TERMO display -pt main:swap-b.0 '#{pane_id}') ||
	fail "display swap pane2 failed"
window1=$($TERMO display -pt "$pane1" '#{window_id}') ||
	fail "display swap window1 failed"
window2=$($TERMO display -pt "$pane2" '#{window_id}') ||
	fail "display swap window2 failed"
index1=$($TERMO display -pt "$pane1" '#{window_index}') ||
	fail "display swap index1 failed"
index2=$($TERMO display -pt "$pane2" '#{window_index}') ||
	fail "display swap index2 failed"
$TERMO set -g @pm 0 || fail "reset @pm swap failed"
$TERMO set-hook -g pane-moved \
	'set -gF @pm "#{@pm}|#{hook_pane}:#{hook_old_window}:#{hook_old_window_index}->#{hook_new_window}:#{hook_new_window_index}"' ||
	fail "set-hook pane-moved swap failed"
$TERMO swapp -d -s "$pane1" -t "$pane2" ||
	fail "swap-pane pane-moved failed"
wait_for @pm "0|$pane1:$window1:$index1->$window2:$index2|$pane2:$window2:$index2->$window1:$index1"
$TERMO set-hook -gu pane-moved || fail "unset pane-moved failed"
$TERMO kill-window -t main:swap-a || fail "kill-window swap-a failed"
$TERMO kill-window -t main:swap-b || fail "kill-window swap-b failed"

# pane-bell fires for the actual pane even when bell alerts are disabled.
pane=$($TERMO neww -d -t main: -n raw-bell -P -F '#{pane_id}' 'cat') ||
	fail "new-window pane-bell failed"
$TERMO setw -t main:raw-bell monitor-bell off ||
	fail "set monitor-bell off failed"
$TERMO set -g @pb 0 || fail "set @pb failed"
$TERMO set-hook -g pane-bell 'set -gF @pb "#{hook}:#{hook_pane}"' ||
	fail "set-hook pane-bell failed"
$TERMO send-keys -t "$pane" -H 07 0a || fail "send-keys pane-bell failed"
wait_for @pb "pane-bell:$pane"
$TERMO set-hook -gu pane-bell || fail "unset pane-bell failed"
$TERMO kill-window -t main:raw-bell || fail "kill-window pane-bell failed"

# pane-resized carries old and new dimensions.
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' 'sleep 30') ||
	fail "split-window pane-resized failed"
old_size=$($TERMO display -pt "$pane" '#{pane_width},#{pane_height}') ||
	fail "display old pane size failed"
$TERMO set -g @pr 0 || fail "set @pr failed"
$TERMO set-hook -g pane-resized \
	'set -gF @pr "#{hook}:#{hook_pane}:#{hook_old_width},#{hook_old_height}->#{hook_width},#{hook_height}"' ||
	fail "set-hook pane-resized failed"
$TERMO resizep -t "$pane" -x 20 -y 10 || fail "resize-pane failed"
new_size=$($TERMO display -pt "$pane" '#{pane_width},#{pane_height}') ||
	fail "display new pane size failed"
wait_for @pr "pane-resized:$pane:$old_size->$new_size"
$TERMO set-hook -gu pane-resized || fail "unset pane-resized failed"
$TERMO killp -t "$pane" || fail "kill-pane pane-resized failed"

# pane-mode-entered and pane-mode-exited expose split transition payloads.
$TERMO set -g @me 0 || fail "set @me failed"
$TERMO set -g @mx 0 || fail "set @mx failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' 'sleep 30') ||
	fail "split-window pane-mode failed"
$TERMO set-hook -g pane-mode-entered \
	'set -gF @me "#{hook}:#{hook_pane}:#{hook_mode_entered}:#{hook_current_mode}:#{hook_previous_mode}"' ||
	fail "set-hook pane-mode-entered failed"
$TERMO set-hook -g pane-mode-exited \
	'set -gF @mx "#{hook}:#{hook_pane}:#{hook_mode_entered}:#{hook_current_mode}:#{hook_previous_mode}"' ||
	fail "set-hook pane-mode-exited failed"
$TERMO copy-mode -t "$pane" || fail "copy-mode split event failed"
wait_for @me "pane-mode-entered:$pane:1:copy-mode:"
$TERMO send-keys -t "$pane" -X cancel || fail "cancel split mode failed"
wait_for @mx "pane-mode-exited:$pane:0::copy-mode"
$TERMO set-hook -gu pane-mode-entered || fail "unset pane-mode-entered failed"
$TERMO set-hook -gu pane-mode-exited || fail "unset pane-mode-exited failed"
$TERMO killp -t "$pane" || fail "kill-pane pane-mode failed"

# marked-pane-changed carries the new/old marked pane and marked flag.
$TERMO set -g @mk 0 || fail "set @mk failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' 'sleep 30') ||
	fail "split-window marked-pane failed"
$TERMO set-hook -g marked-pane-changed \
	'set -gF @mk "#{hook}:#{hook_marked}:#{hook_pane}:#{hook_new_pane}:#{hook_old_pane}"' ||
	fail "set-hook marked-pane-changed failed"
$TERMO selectp -t "$pane" -m || fail "mark pane failed"
wait_for @mk "marked-pane-changed:1:$pane:$pane:"
$TERMO selectp -t "$pane" -m || fail "unmark pane failed"
wait_for @mk "marked-pane-changed:0:$pane::$pane"
$TERMO set-hook -gu marked-pane-changed ||
	fail "unset marked-pane-changed failed"
$TERMO killp -t "$pane" || fail "kill-pane marked-pane failed"

# window-zoomed and window-unzoomed fire on resize-pane -Z.
$TERMO set -g @zoom 0 || fail "set @zoom failed"
pane=$($TERMO splitw -d -t main:0 -P -F '#{pane_id}' 'sleep 30') ||
	fail "split-window zoom failed"
window=$($TERMO display -pt "$pane" '#{window_id}') ||
	fail "display zoom window failed"
$TERMO set-hook -g window-zoomed \
	'set -gF @zoom "#{hook}:#{hook_window}"' ||
	fail "set-hook window-zoomed failed"
$TERMO set-hook -g window-unzoomed \
	'set -gF @zoom "#{hook}:#{hook_window}"' ||
	fail "set-hook window-unzoomed failed"
$TERMO resizep -Z -t "$pane" || fail "zoom pane failed"
wait_for @zoom "window-zoomed:$window"
$TERMO resizep -Z -t "$pane" || fail "unzoom pane failed"
wait_for @zoom "window-unzoomed:$window"
$TERMO set-hook -gu window-zoomed || fail "unset window-zoomed failed"
$TERMO set-hook -gu window-unzoomed || fail "unset window-unzoomed failed"
$TERMO killp -t "$pane" || fail "kill-pane zoom failed"

# client-attached and client-detached using a control client.
$TERMO set -g @a 0 || fail "set @a failed"
$TERMO set -g @cl 0 || fail "set @cl failed"
$TERMO set-hook -g client-created \
	'set -gF @cl "#{?#{m:client-*,#{hook_client}},#{@cl}|#{hook}:#{hook_client},#{@cl}}"' ||
	fail "set-hook client-created failed"
$TERMO set-hook -g client-closed \
	'set -gF @cl "#{?#{m:client-*,#{hook_client}},#{@cl}|#{hook}:#{hook_client},#{@cl}}"' ||
	fail "set-hook client-closed failed"
$TERMO set-hook -g client-attached 'set -gF @a "#{hook}"' ||
	fail "set-hook client-attached failed"
$TERMO set-hook -g client-detached 'set -gF @a "#{hook}"' ||
	fail "set-hook client-detached failed"
mkfifo "$OUT/fifo" || fail "mkfifo failed"
$TERMO -C attach -t main <"$OUT/fifo" >"$OUT/control.out" 2>&1 &
exec 3>"$OUT/fifo"
wait_for @a 'client-attached'

# client-session-changed carries old and new sessions.
$TERMO new -d -s client-target || fail "new-session client target failed"
client=$($TERMO list-clients -F '#{client_name} #{client_control_mode}' |
	awk '$2 == 1 { print $1; exit }') ||
	fail "list control client failed"
[ -n "$client" ] || fail "no control client found"
wait_for @cl "0|client-created:$client"
old_session=$($TERMO display -pt main '#{session_id}') ||
	fail "display old client session failed"
new_session=$($TERMO display -pt client-target '#{session_id}') ||
	fail "display new client session failed"
$TERMO set -g @cs 0 || fail "set @cs failed"
$TERMO set-hook -g client-session-changed \
	'set -gF @cs "#{hook}:#{hook_client}:#{hook_session}:#{hook_old_session}:#{hook_new_session}"' ||
	fail "set-hook client-session-changed failed"
$TERMO switch-client -c "$client" -t client-target ||
	fail "switch-client session failed"
wait_for @cs "client-session-changed:$client:$new_session:$old_session:$new_session"
$TERMO set-hook -gu client-session-changed ||
	fail "unset client-session-changed failed"

exec 3>&-
wait_for @a 'client-detached'
wait_for @cl "0|client-created:$client|client-closed:$client"
$TERMO set-hook -gu client-created || fail "unset client-created failed"
$TERMO set-hook -gu client-closed || fail "unset client-closed failed"
$TERMO set-hook -gu client-attached || fail "unset client-attached failed"
$TERMO set-hook -gu client-detached || fail "unset client-detached failed"

# client-resized carries old and new dimensions.
$TERMO2 new -d -s resize -x 80 -y 24 'sleep 30' ||
	fail "new-session client-resized failed"
$TERMO2 set -g @cr 0 || fail "set @cr failed"
$TERMO2 set-hook -g client-resized \
	'set -gF @cr "#{hook}:#{hook_client}:#{hook_old_width},#{hook_old_height}->#{hook_width},#{hook_height}"' ||
	fail "set-hook client-resized failed"
$TERMO neww -d -n resize-client "$TERMO2 attach -t resize" ||
	fail "new outer window for client-resized failed"
i=0
client=
while [ $i -lt 30 ]; do
	client=$($TERMO2 list-clients -F '#{client_name}' 2>/dev/null |
		awk 'NR == 1 { print; exit }')
	[ -n "$client" ] && break
	i=$((i + 1))
	sleep 0.2
done
[ -n "$client" ] || fail "inner client did not attach"
old_size=$($TERMO2 list-clients -F '#{client_width},#{client_height}') ||
	fail "display old client size failed"
$TERMO resizew -t resize-client -x 70 -y 20 ||
	fail "resize outer client window failed"
i=0
while [ $i -lt 30 ]; do
	value=$($TERMO2 show -gqv @cr 2>/dev/null || true)
	new_size=$($TERMO2 list-clients -F '#{client_width},#{client_height}' \
		2>/dev/null || true)
	expected="client-resized:$client:$old_size->$new_size"
	[ "$new_size" != "$old_size" ] && [ "$value" = "$expected" ] &&
		break
	i=$((i + 1))
	sleep 0.2
done
[ "$value" = "$expected" ] ||
	fail "expected @cr to be '$expected' but got '$value'"
$TERMO2 set-hook -gu client-resized || fail "unset client-resized failed"

exit 0
