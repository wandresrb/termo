#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"

fail()
{
	echo "$1"
	[ -z "$TERMO" ] || $TERMO kill-server 2>/dev/null
	exit 1
}

goto_cell()
{
	row=$1
	column=$2

	$TERMO send-keys -X history-top || fail "$mode: history-top failed"
	$TERMO send-keys -X start-of-line || fail "$mode: start-of-line failed"
	[ "$row" -eq 0 ] ||
	    $TERMO send-keys -X -N "$row" cursor-down ||
	    fail "$mode: cursor-down failed"
	[ "$column" -eq 0 ] ||
	    $TERMO send-keys -X -N "$column" cursor-right ||
	    fail "$mode: cursor-right failed"
}

check_cursor()
{
	expected=$1
	actual=$($TERMO display-message -p '#{copy_cursor_x},#{copy_cursor_y}')
	[ "$actual" = "$expected" ] ||
	    fail "$mode: expected cursor $expected, got $actual"
}

check_word()
{
	row=$1
	column=$2
	expected=$3

	goto_cell "$row" "$column"
	$TERMO set-buffer sentinel || fail "$mode: set-buffer failed"
	$TERMO send-keys -X select-word || fail "$mode: select-word failed"
	$TERMO send-keys -X copy-selection || fail "$mode: copy-selection failed"
	actual=$($TERMO show-buffer 2>/dev/null)
	[ "$actual" = "$expected" ] ||
	    fail "$mode: expected word '$expected', got '$actual'"
}

check_next_word()
{
	row=$1
	expected=$2

	goto_cell "$row" 0
	$TERMO send-keys -X next-word || fail "$mode: next-word failed"
	$TERMO set-buffer sentinel || fail "$mode: set-buffer failed"
	$TERMO send-keys -X select-word || fail "$mode: select-word failed"
	$TERMO send-keys -X copy-selection || fail "$mode: copy-selection failed"
	actual=$($TERMO show-buffer 2>/dev/null)
	[ "$actual" = "$expected" ] ||
	    fail "$mode: expected next word '$expected', got '$actual'"
}

for mode in emacs vi; do
	TERMO="$TEST_TERMO -Lunicode-whitespace-$mode-$$ -f/dev/null"
	TMUX="$TERMO"
	$TERMO kill-server 2>/dev/null
	$TERMO new-session -d -x16 -y20 \
	    "printf 'aa bb\naa\tbb\naa\302\240bb\naa\343\200\200bb\naa:bb\naaaaaaaaaaaaaaa\302\240b\nabcdefghijklmnopq\nhardone\nhardtwo\naa\341\232\200bb\naa\342\200\200bb\naa\342\200\250bb\naa\342\200\251bb\naa\342\200\257bb\naa\342\201\237bb\nxa\302\205b c\n'; exec cat" ||
	    fail "$mode: new-session failed"
	$TERMO set-option -g window-size manual || fail "$mode: set size failed"
	$TERMO set-window-option -g mode-keys "$mode" ||
	    fail "$mode: set mode-keys failed"
	$TERMO set-window-option -g word-separators "" ||
	    fail "$mode: clear word-separators failed"
	$TERMO copy-mode || fail "$mode: copy-mode failed"

	# ASCII space and TAB retain their existing behaviour.
	goto_cell 0 0
	$TERMO send-keys -X next-space || fail "$mode: ASCII next-space failed"
	check_cursor 3,0
	goto_cell 1 0
	$TERMO send-keys -X next-space || fail "$mode: TAB next-space failed"
	check_cursor 8,1
	$TERMO send-keys -X previous-space ||
	    fail "$mode: TAB previous-space failed"
	check_cursor 0,1

	# NBSP is whitespace for selection and word movement.
	goto_cell 2 0
	$TERMO send-keys -X next-word || fail "$mode: NBSP next-word failed"
	check_cursor 3,2
	$TERMO send-keys -X previous-word ||
	    fail "$mode: NBSP previous-word failed"
	check_cursor 0,2
	check_word 2 0 aa
	goto_cell 2 0
	$TERMO send-keys -X next-word-end ||
	    fail "$mode: NBSP next-word-end failed"
	if [ "$mode" = emacs ]; then
		check_cursor 2,2
	else
		check_cursor 1,2
	fi

	# U+3000 is two columns wide; both its leading and padding cells must be
	# skipped as one whitespace character in either direction.
	goto_cell 3 0
	$TERMO send-keys -X next-word || fail "$mode: U+3000 next-word failed"
	check_cursor 4,3
	$TERMO send-keys -X previous-word ||
	    fail "$mode: U+3000 previous-word failed"
	check_cursor 0,3
	goto_cell 3 0
	$TERMO send-keys -X next-space-end ||
	    fail "$mode: U+3000 next-space-end failed"
	if [ "$mode" = emacs ]; then
		check_cursor 2,3
	else
		check_cursor 1,3
	fi

	# Other characters in word-separators remain literal when space enables
	# Unicode whitespace matching.
	$TERMO set-window-option -g word-separators ': ' ||
	    fail "$mode: set custom word-separators failed"
	goto_cell 4 0
	$TERMO send-keys -X next-word || fail "$mode: literal next-word failed"
	check_cursor 2,4
	$TERMO set-window-option -g word-separators "" ||
	    fail "$mode: restore word-separators failed"

	# Whitespace in the last column remains a boundary across a soft wrap.
	goto_cell 5 0
	$TERMO send-keys -X next-space ||
	    fail "$mode: wrapped NBSP next-space failed"
	check_cursor 0,6

	# A word split only by a soft wrap remains one word, while a hard line
	# boundary still terminates selection.
	check_word 7 0 abcdefghijklmnopq
	check_word 9 0 hardone

	# Check the remaining representative White_Space ranges and U+0085
	# combined into a cell with another character.
	check_next_word 11 bb
	check_next_word 12 bb
	check_next_word 13 bb
	check_next_word 14 bb
	check_next_word 15 bb
	check_next_word 16 bb
	goto_cell 17 0
	$TERMO send-keys -X next-word ||
	    fail "$mode: combined U+0085 next-word failed"
	check_cursor 2,17

	$TERMO kill-server 2>/dev/null
done

exit 0
