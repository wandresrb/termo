#!/bin/sh

# command-alias expansion

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

$TERMO new-session -d -sfoo || exit 1
$TERMO split-window -d -tfoo:0.0 || exit 1
$TERMO set -s command-alias[100] zoom='resize-pane -Z' || exit 1
$TERMO zoom -tfoo:0.0 || exit 1
[ "$($TERMO display-message -p -tfoo:0.0 '#{window_zoomed_flag}')" = 1 ] || exit 1

$TERMO kill-server 2>/dev/null

exit 0
