#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

$TERMO new -d -x40 -y10 \
      "cat copy-mode-test.txt; printf '\e[9;15H'; cat" || exit 1
$TERMO set -g window-size manual || exit 1

# Enter copy mode and go to the first column of the first row.
$TERMO set-window-option -g mode-keys emacs
$TERMO set-window-option -g word-separators ""
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
[ "$($TERMO show-buffer 2>/dev/null)" = "" ] || exit 1

# Test that `next-word-end` does not skip single-letter words.
$TERMO send-keys -X next-word-end
$TERMO send-keys -X begin-selection
$TERMO send-keys -X previous-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "A" ] || exit 1

# Test that `next-word-end` wraps around indented line breaks.
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word-end
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "words\n\tIndented")" ] || exit 1

# Test that `next-word` wraps around un-indented line breaks.
$TERMO send-keys -X next-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "line\n")" ] || exit 1

# Test that `next-word-end` treats periods as letters.
$TERMO send-keys -X next-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "line..." ] || exit 1

# Test that `previous-word` and `next-word` treat periods as letters.
$TERMO send-keys -X previous-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "line...\n")" ] || exit 1

# Test that `previous-space` and `next-space` treat periods as letters.
$TERMO send-keys -X previous-space
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-space
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "line...\n")" ] || exit 1

# Test that `next-word` and `next-word-end` treat other symbols as letters.
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word-end
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "... @nd then \$ym_bols[]{}" ] || exit 1

# Test that `previous-word` treats other symbols as letters
# and `next-word` wraps around for indented symbols
$TERMO send-keys -X previous-word
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "$(printf "\$ym_bols[]{}\n ")" ] || exit 1

# Test that `next-word-end` treats digits as letters
$TERMO send-keys -X next-word-end
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = " 500xyz" ] || exit 1

# Test that `previous-word` treats digits as letters
$TERMO send-keys -X begin-selection
$TERMO send-keys -X previous-word
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "500xyz" ] || exit 1

# Test that `next-word` and `next-word-end` stop at the end of text.
$TERMO send-keys -X begin-selection
$TERMO send-keys -X next-word
$TERMO send-keys -X next-word-end
$TERMO send-keys -X next-word
$TERMO send-keys -X next-space
$TERMO send-keys -X next-space-end
$TERMO send-keys -X copy-selection
[ "$($TERMO show-buffer)" = "500xyz" ] || exit 1

$TERMO kill-server 2>/dev/null
exit 0
