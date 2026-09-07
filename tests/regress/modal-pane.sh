#!/bin/sh

# Tests for modal floating panes.

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"

cleanup()
{
	$TERMO kill-server >/dev/null 2>&1
	$TERMO2 kill-server >/dev/null 2>&1
}

fail()
{
	echo "$*" >&2
	cleanup
	exit 1
}

must_equal()
{
	got=$1
	want=$2
	[ "$got" = "$want" ] || fail "got '$got', expected '$want'"
}

check_ok()
{
	$TERMO "$@" || fail "command failed: $*"
}

check_fail()
{
	exp="$1"
	shift
	out=$($TERMO "$@" 2>&1)
	if [ $? -eq 0 ]; then
		fail "command succeeded (expected failure): $*"
	fi
	must_equal "$out" "$exp"
}

fmt()
{
	$TERMO display-message -p -t "$1" "$2"
}

click()
{
	col="$1"
	row="$2"
	seq=$(printf '\033[<0;%s;%sM\033[<0;%s;%sm' \
	    "$col" "$row" "$col" "$row")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}

move_mouse()
{
	col="$1"
	row="$2"
	seq=$(printf '\033[<35;%s;%sM' "$col" "$row")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}

ctrl_drag()
{
	scol="$1"
	srow="$2"
	ecol="$3"
	erow="$4"

	seq=$(printf '\033[<16;%s;%sM' "$scol" "$srow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<48;%s;%sM' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<16;%s;%sm' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}

drag()
{
	scol="$1"
	srow="$2"
	ecol="$3"
	erow="$4"

	seq=$(printf '\033[<0;%s;%sM' "$scol" "$srow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<32;%s;%sM' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<0;%s;%sm' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}

meta_drag()
{
	scol="$1"
	srow="$2"
	ecol="$3"
	erow="$4"

	seq=$(printf '\033[<8;%s;%sM' "$scol" "$srow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<40;%s;%sM' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 0.2
	seq=$(printf '\033[<8;%s;%sm' "$ecol" "$erow")
	$TERMO2 send-keys -t "$OUTER" -l "$seq" 2>/dev/null
	sleep 1
}

cleanup

check_ok new-session -d -s modal -x 80 -y 24 'cat'
sleep 1
p0=$(fmt modal:0 '#{pane_id}')
check_ok split-window -h -t "$p0" 'cat'
sleep 1
p1=$(fmt modal:0 '#{pane_id}')

check_ok select-pane -t "$p0"
check_ok resize-pane -Z -t "$p0"
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 1

modal=$($TERMO new-pane -OPF '#{pane_id}' -t "$p1" \
    -x 20 -y 5 -X 20 -Y 10 'cat') ||
	fail "new-pane -O failed"
sleep 1
must_equal "$(fmt "$modal" '#{pane_floating_flag}:#{pane_modal_flag}:#{pane_active}')" 1:1:1
case "$(fmt "$modal" '#{pane_flags}')" in
*O*) ;;
*) fail "modal pane flags do not include O" ;;
esac
case "$(fmt "$modal" '#{pane_flags}')" in
*A*) ;;
*) fail "modal pane flags do not include A" ;;
esac
case "$(fmt modal:0 '#{window_flags}')" in
*O*) ;;
*) fail "modal window flags do not include O" ;;
esac
must_equal "$(fmt modal:0 '#{window_modal_pane}')" "$modal"
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 1

check_fail "window already has a modal pane" \
	new-pane -O -x 10 -y 4 'cat'
check_fail "modal pane must be floating" \
	new-pane -O -L 'cat'
check_fail "pane is modal" \
	break-pane -s "$modal"
check_fail "pane is modal" \
	join-pane -s "$modal" -t "$p0"
check_fail "pane is modal" \
	join-pane -s "$p0" -t "$modal"
check_fail "pane is modal" \
	swap-pane -s "$modal" -t "$p0"
check_fail "pane is modal" \
	swap-pane -s "$p0" -t "$modal"

check_ok select-pane -t "$p1"
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"
check_ok last-pane -t modal:0
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"

under=$($TERMO split-window -PF '#{pane_id}' -t "$p0" 'cat') ||
	fail "split-window under modal failed"
sleep 1
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"
must_equal "$(fmt "$under" '#{pane_active}')" 0
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 1

float=$($TERMO new-pane -PF '#{pane_id}' -x 10 -y 4 -X 5 -Y 3 'cat') ||
	fail "new floating pane under modal failed"
sleep 1
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"
must_equal "$(fmt "$float" '#{pane_active}')" 0
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 1

check_ok new-window -d -t modal: -n other 'cat'
check_ok select-window -t modal:other
other=$(fmt modal:other '#{pane_id}')
must_equal "$(fmt modal:other '#{pane_id}')" "$other"
check_ok select-window -t modal:0
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"

check_ok send-keys -t modal:0 'modal-key' Enter
sleep 1
case "$($TERMO capture-pane -pt "$modal")" in
*modal-key*) ;;
*) fail "keyboard input did not reach modal pane" ;;
esac
case "$($TERMO capture-pane -pt "$p0")" in
*modal-key*) fail "keyboard input reached pane below modal" ;;
esac

$TERMO set -g mouse on
$TERMO set -g focus-follows-mouse on
$TERMO set -g @modal-mouse ''
$TERMO bind -n MouseDown1Pane run-shell \
    "$TERMO set -g @modal-mouse '#{mouse_pane}'"
$TERMO bind x set -g @modal-prefix yes

$TERMO2 new-session -d -x 80 -y 24 "$TERMO attach -t modal" ||
	fail "outer session failed"
sleep 1
OUTER=$($TERMO2 list-panes -F '#{pane_id}' | head -1)
[ -n "$OUTER" ] || fail "no outer pane"

click 1 1
must_equal "$($TERMO show -gv @modal-mouse)" ''
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"

panes=$(fmt modal:0 '#{window_panes}')
ctrl_drag 1 1 8 3
must_equal "$(fmt modal:0 '#{window_panes}')" "$panes"
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"

move_mouse 1 1
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"

left=$(fmt "$modal" '#{pane_left}')
top=$(fmt "$modal" '#{pane_top}')
click $((left + 1)) $((top + 1))
must_equal "$($TERMO show -gv @modal-mouse)" "$modal"
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"

width=$(fmt "$modal" '#{pane_width}')
right=$((left + width + 1))
drag "$right" $((top + 1)) $((right + 5)) $((top + 1))
new_width=$(fmt "$modal" '#{pane_width}')
[ "$new_width" -gt "$width" ] ||
	fail "modal pane did not grow after right-border drag"

$TERMO2 send-keys -t "$OUTER" C-b x
sleep 1
must_equal "$($TERMO show -gv @modal-prefix)" yes

$TERMO set-buffer -b modal-edit-test 'test'
check_ok choose-buffer -t "$modal"
panes=$(fmt modal:0 '#{window_panes}')
$TERMO2 send-keys -t "$OUTER" e
sleep 1
must_equal "$(fmt modal:0 '#{window_panes}')" "$panes"
must_equal "$(fmt modal:0 '#{window_modal_pane}')" "$modal"
$TERMO2 send-keys -t "$OUTER" q
sleep 1

check_ok kill-pane -t "$modal"
sleep 1
must_equal "$(fmt modal:0 '#{window_modal_pane}')" ''
case "$(fmt modal:0 '#{window_flags}')" in
*O*) fail "modal window flag remained after modal pane closed" ;;
esac
must_equal "$(fmt modal:0 '#{pane_id}')" "$p0"
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 1
check_ok resize-pane -Z -t "$p0"
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 0

$TERMO set -g editor 'sh -c "sleep 10" sh'
check_ok resize-pane -Z -t "$p0"
check_ok choose-buffer -t "$p0"
panes=$(fmt modal:0 '#{window_panes}')
$TERMO2 send-keys -t "$OUTER" e
sleep 1
editor=$(fmt modal:0 '#{window_modal_pane}')
[ -n "$editor" ] || fail "buffer editor did not open as modal pane"
must_equal "$(fmt modal:0 '#{window_panes}')" $((panes + 1))
must_equal "$(fmt "$editor" '#{pane_modal_flag}:#{pane_active}')" 1:1
check_ok kill-pane -t "$editor"
sleep 1
must_equal "$(fmt modal:0 '#{window_modal_pane}')" ''
must_equal "$(fmt modal:0 '#{pane_id}')" "$p0"
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 1
check_ok resize-pane -Z -t "$p0"
must_equal "$(fmt modal:0 '#{window_zoomed_flag}')" 0

detached=$($TERMO new-pane -OdPF '#{pane_id}' -x 20 -y 5 -X 20 -Y 10 \
    'cat') || fail "new detached modal failed"
sleep 1
must_equal "$(fmt "$detached" '#{pane_modal_flag}:#{pane_active}')" 1:1
must_equal "$(fmt modal:0 '#{window_modal_pane}')" "$detached"
check_ok kill-pane -t "$detached"
sleep 1
must_equal "$(fmt modal:0 '#{window_modal_pane}')" ''
must_equal "$(fmt modal:0 '#{pane_id}')" "$p0"

$TERMO set -g @modal-custom old
check_ok customize-mode -t "$p0" \
	-f '#{==:#{option_name},@modal-custom}'
panes=$(fmt modal:0 '#{window_panes}')
$TERMO2 send-keys -t "$OUTER" j Right j e
sleep 1
editor=$(fmt modal:0 '#{window_modal_pane}')
[ -n "$editor" ] || fail "customize editor did not open as modal pane"
must_equal "$(fmt modal:0 '#{window_panes}')" $((panes + 1))
must_equal "$(fmt "$editor" '#{pane_modal_flag}:#{pane_active}')" 1:1
check_ok kill-pane -t "$editor"
sleep 1
must_equal "$(fmt modal:0 '#{window_modal_pane}')" ''
$TERMO2 send-keys -t "$OUTER" q
sleep 1

modal=$($TERMO new-pane -OkPF '#{pane_id}' -x 20 -y 5 -X 20 -Y 10 'printf done') ||
	fail "new retained modal failed"
sleep 2
must_equal "$(fmt "$modal" '#{pane_dead}:#{pane_modal_flag}:#{pane_active}')" 1:1:1
check_ok respawn-pane -k -t "$modal" 'cat'
sleep 1
must_equal "$(fmt "$modal" '#{pane_dead}:#{pane_modal_flag}:#{pane_active}')" 0:1:1
check_ok select-pane -t "$p1"
must_equal "$(fmt modal:0 '#{pane_id}')" "$modal"
check_ok kill-pane -t "$modal"
sleep 1
must_equal "$(fmt modal:0 '#{window_modal_pane}')" ''
must_equal "$(fmt modal:0 '#{pane_id}')" "$p0"
must_equal "$(fmt "$p0" '#{window_zoomed_flag}:#{pane_zoomed_flag}')" 0:0

ignored=$($TERMO new-pane -KdPF '#{pane_id}' -t "$p0" 'cat') ||
	fail "new-pane -K without -O failed"
check_ok kill-pane -t "$ignored"

$TERMO set -g @modal-prefix no
$TERMO set -g @modal-root no
$TERMO bind -n z set -g @modal-root yes

modal=$($TERMO new-pane -OKPF '#{pane_id}' -t "$p0" \
    -x 20 -y 5 -X 20 -Y 10 'cat') ||
	fail "new-pane -OK failed"
sleep 1
$TERMO2 send-keys -t "$OUTER" C-b x z Enter
sleep 1
must_equal "$($TERMO show -gv @modal-prefix)" no
must_equal "$($TERMO show -gv @modal-root)" no
case "$($TERMO capture-pane -pt "$modal")" in
*xz*) ;;
*) fail "keys did not reach key-capturing modal pane" ;;
esac
left=$(fmt "$modal" '#{pane_left}')
top=$(fmt "$modal" '#{pane_top}')
meta_drag $((left + 2)) $((top + 2)) $((left + 7)) $((top + 4))
new_left=$(fmt "$modal" '#{pane_left}')
new_top=$(fmt "$modal" '#{pane_top}')
[ "$new_left" -gt "$left" ] || [ "$new_top" -gt "$top" ] ||
	fail "key-capturing modal pane did not move"
check_ok kill-pane -t "$modal"
sleep 1

# A nonmodal floating pane may remain above zoom, and switching between it and
# the zoomed tiled pane must not unzoom the window.
check_ok new-window -d -t modal: -n float-over-zoom 'cat'
base=$(fmt modal:float-over-zoom '#{pane_id}')
check_ok split-window -dh -t "$base" 'cat'
check_ok resize-pane -Z -t "$base"
over=$($TERMO new-pane -APF '#{pane_id}' -t "$base" \
    -x 20 -y 5 -X 20 -Y 10 'cat') ||
	fail "new-pane -A failed"
must_equal "$(fmt "$over" '#{pane_floating_flag}:#{pane_active}')" 1:1
case "$(fmt "$over" '#{pane_flags}')" in
*A*) ;;
*) fail "float-over-zoom pane flags do not include A" ;;
esac
must_equal "$(fmt "$base" '#{window_zoomed_flag}:#{pane_zoomed_flag}')" 1:1
check_ok select-pane -t "$base"
must_equal "$(fmt "$base" '#{window_zoomed_flag}:#{pane_active}')" 1:1
check_ok select-pane -t "$over"
must_equal "$(fmt "$over" '#{window_zoomed_flag}:#{pane_active}')" 1:1
client=$($TERMO list-clients -F '#{client_name}' | head -1)
[ -n "$client" ] || fail "no client for switch-client test"
check_ok switch-client -c "$client" -t "$base"
must_equal "$(fmt "$base" '#{window_zoomed_flag}:#{pane_active}')" 1:1
check_ok switch-client -c "$client" -t "$over"
must_equal "$(fmt "$over" '#{window_zoomed_flag}:#{pane_active}')" 1:1
check_ok kill-pane -t "$over"
must_equal "$(fmt "$base" '#{window_zoomed_flag}:#{pane_zoomed_flag}')" 1:1
check_ok resize-pane -Z -t "$base"

ignored=$($TERMO new-pane -ALdPF '#{pane_id}' -t "$base" 'cat') ||
	fail "new-pane -A -L failed"
case "$(fmt "$ignored" '#{pane_flags}')" in
*A*) fail "tiled pane flags include A" ;;
*) ;;
esac

# Existing floating panes are filtered when zoom begins: -A panes remain in the
# visible layout and ordinary floating panes do not.
check_ok new-window -d -t modal: -n existing-over-zoom 'cat'
base=$(fmt modal:existing-over-zoom '#{pane_id}')
check_ok split-window -dh -t "$base" 'cat'
over=$($TERMO new-pane -AdPF '#{pane_id}' -t "$base" \
    -x 20 -y 5 -X 20 -Y 10 'cat') ||
	fail "pre-existing new-pane -A failed"
under=$($TERMO new-pane -dPF '#{pane_id}' -t "$base" \
    -x 15 -y 4 -X 2 -Y 2 'cat') ||
	fail "pre-existing ordinary new-pane failed"
check_ok select-window -t modal:existing-over-zoom
check_ok select-pane -t "$base"
check_ok resize-pane -Z -t "$base"
must_equal "$(fmt "$over" '#{pane_floating_flag}')" 1
must_equal "$(fmt "$under" '#{pane_floating_flag}')" 0
must_equal "$(fmt "$base" '#{window_zoomed_flag}:#{pane_zoomed_flag}')" 1:1

# Geometry changed in the visible zoom layout is copied back when unzooming.
check_ok select-pane -t "$over"
left=$(fmt "$over" '#{pane_left}')
top=$(fmt "$over" '#{pane_top}')
meta_drag $((left + 2)) $((top + 2)) $((left + 7)) $((top + 4))
new_left=$(fmt "$over" '#{pane_left}')
new_top=$(fmt "$over" '#{pane_top}')
[ "$new_left" -gt "$left" ] || [ "$new_top" -gt "$top" ] ||
	fail "float-over-zoom pane did not move"
must_equal "$(fmt "$base" '#{window_zoomed_flag}:#{pane_zoomed_flag}')" 1:1
check_ok resize-pane -Z -t "$base"
must_equal "$(fmt "$over" '#{pane_left}:#{pane_top}')" \
    "$new_left:$new_top"
must_equal "$(fmt "$under" '#{pane_floating_flag}')" 1

# Resizing with the over-zoom pane active restores the original zoom target.
check_ok resize-pane -Z -t "$base"
check_ok select-pane -t "$over"
check_ok resize-window -t modal:existing-over-zoom -x 90 -y 30
must_equal "$(fmt "$base" \
    '#{window_width}x#{window_height}:#{window_zoomed_flag}:#{pane_zoomed_flag}')" \
    90x30:1:1
must_equal "$(fmt "$over" '#{pane_floating_flag}:#{pane_active}')" 1:1

# Natural pane exit uses a different removal path from kill-pane and must also
# preserve zoom.
dying=$($TERMO new-pane -AdPF '#{pane_id}' -t "$base" \
    -x 12 -y 4 -X 4 -Y 3 'true') ||
	fail "short-lived new-pane -A failed"
i=0
while $TERMO list-panes -a -F '#{pane_id}' | grep -qx "$dying"; do
    i=$((i + 1))
    [ $i -gt 50 ] && fail "short-lived float-over-zoom pane did not exit"
    sleep 0.1
done
must_equal "$(fmt "$base" '#{window_zoomed_flag}:#{pane_zoomed_flag}')" 1:1

# A pane with -A is also above a zoom target which was itself floating. The
# temporary tiled target must sit behind retained floating panes, then return
# to the normal floating z order when unzoomed.
check_ok new-window -d -t modal: -n floating-zoom-target 'cat'
base=$(fmt modal:floating-zoom-target '#{pane_id}')
check_ok split-window -dh -t "$base" 'cat'
target=$($TERMO new-pane -dPF '#{pane_id}' -t "$base" \
    -x 30 -y 10 -X 10 -Y 5 'cat') ||
	fail "floating zoom target creation failed"
over=$($TERMO new-pane -AdPF '#{pane_id}' -t "$base" \
    -x 15 -y 5 -X 15 -Y 8 'cat') ||
	fail "float-over-zoom pane creation failed"
check_ok select-window -t modal:floating-zoom-target
check_ok select-pane -t "$target"
must_equal "$(fmt "$target" '#{pane_z}')" 0
must_equal "$(fmt "$over" '#{pane_z}')" 1

check_ok resize-pane -Z -t "$target"
must_equal "$(fmt "$target" \
    '#{window_zoomed_flag}:#{pane_zoomed_flag}:#{pane_floating_flag}:#{pane_z}')" \
    1:1:0:2
must_equal "$(fmt "$over" '#{pane_floating_flag}:#{pane_z}')" 1:0
check_ok select-pane -t "$over"
must_equal "$(fmt "$over" '#{window_zoomed_flag}:#{pane_active}')" 1:1
check_ok resize-pane -Z -t "$target"
must_equal "$(fmt "$over" '#{pane_active}:#{pane_z}')" 1:0
must_equal "$(fmt "$target" '#{pane_floating_flag}:#{pane_z}')" 1:1

# If the target remains active, it returns to the front on unzoom.
check_ok select-pane -t "$target"
check_ok resize-pane -Z -t "$target"
must_equal "$(fmt "$over" '#{pane_floating_flag}:#{pane_z}')" 1:0
check_ok resize-pane -Z -t "$target"
must_equal "$(fmt "$target" '#{pane_active}:#{pane_z}')" 1:0
must_equal "$(fmt "$over" '#{pane_z}')" 1

cleanup
exit 0
