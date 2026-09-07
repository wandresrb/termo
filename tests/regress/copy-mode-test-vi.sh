#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -f/dev/null -LtestA$$"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

$TERMO new -d -x40 -y10 \
      "cat copy-mode-test.txt; printf '\e[9;15H'; cat" || exit 1
$TERMO set -g window-size manual || exit 1

# Enter copy mode and go to the first column of the first row.
$TERMO set-window-option -g mode-keys vi
$TERMO copy-mode
$TERMO send-keys -X history-top
$TERMO send-keys -X start-of-line

# Test that `previous-word` and `previous-space`
# do not go past the start of text.
$TERMO send-keys -X begin-selection
$TERMO send-keys -X previous-word
$TERMO send-keys -X previous-space
$TERMO send-keys -X previous-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "A" ] || exit 1

# Test that `next-word-end` skips single-letter words
# and `previous-word` does not skip multi-letter words.
$TERMO send-keys -X next-word-end
$TERMO send-keys -X begin-selection
$TERMO send-keys -X previous-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "line" ] || exit 1

# Test that `next-word-end` stops at the end of the line.
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word-end
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "words" ] || exit 1

# Move to the next word for the following tests.
$TERMO send-keys -X next-word

# Test that `next-word` wraps around un-indented line breaks.
$TERMO send-keys -X next-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "line\nA")" ] || exit 1

# Test that `next-word-end` does not treat periods as letters.
$TERMO send-keys -X next-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "line" ] || exit 1

# Test that `next-space-end` treats periods as letters.
$TERMO send-keys -X previous-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-space-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "line..." ] || exit 1

# Test that `previous-space` and `next-space` treat periods as letters.
$TERMO send-keys -X previous-space
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-space
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "line...\n.")" ] || exit 1

# Test that `next-word` and `next-word-end` do not treat other symbols as letters.
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word-end
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "... @nd then" ] || exit 1

# Test that `next-space` wraps around for indented symbols
$TERMO send-keys -X next-space
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-space
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "\$ym_bols[]{}\n ?")" ] || exit 1

# Test that `next-word-end` treats digits as letters
$TERMO send-keys -X next-word-end
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "? 500xyz" ] || exit 1

# Test that `previous-word` treats digits as letters
$TERMO send-keys -X begin-selection
$TERMO send-keys -X previous-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "500xyz" ] || exit 1

# Test that `next-word`, `next-word-end`,
# `next-space`, and `next-space-end` stop at the end of text.
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word-end
$TERMO send-keys -X next-word
$TERMO send-keys -X next-space
$TERMO send-keys -X next-space-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "500xyz" ] || exit 1

# Test that vi cursor movement does not stop on the padding cell of a wide
# character at the end of a line.
$TERMO kill-server 2>/dev/null
sleep 1
$TERMO new -d -x20 -y5 \
      "printf 'abc中\nxyz\n'; exec cat" || exit 1
$TERMO set-window-option -g mode-keys vi
$TERMO copy-mode
$TERMO send-keys -X history-top
$TERMO send-keys -X start-of-line
$TERMO send-keys -X cursor-right
$TERMO send-keys -X cursor-right
$TERMO send-keys -X cursor-right
[ "$($TERMO display -p '#{copy_cursor_x},#{copy_cursor_y}')" = "3,0" ] ||
    exit 1
$TERMO send-keys -X cursor-right
[ "$($TERMO display -p '#{copy_cursor_x},#{copy_cursor_y}')" = "0,1" ] ||
    exit 1
$TERMO send-keys -X cursor-left
[ "$($TERMO display -p '#{copy_cursor_x},#{copy_cursor_y}')" = "3,0" ] ||
    exit 1
$TERMO send-keys -X cursor-left
[ "$($TERMO display -p '#{copy_cursor_x},#{copy_cursor_y}')" = "2,0" ] ||
    exit 1

$TERMO kill-server 2>/dev/null
exit 0
