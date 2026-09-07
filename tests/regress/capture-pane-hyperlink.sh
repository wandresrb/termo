#!/bin/sh

# capture-pane -e for OSC 8 hyperlink

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
TMP=$(mktemp)
TMP2=$(mktemp)
trap "rm -f $TMP $TMP2" 0 1 15
$TERMO kill-server 2>/dev/null

do_test() {
  $TERMO -f/dev/null new -d "
  printf '$1'
  $TERMO capturep -peS0 -E1 >$TMP"
  printf "$2\n" > $TMP2
  sleep 1
  cmp $TMP $TMP2 || exit 1
  return 0
}

do_test '\033]8;id=1;https://github.com\033\\test1\033]8;;\033\\\n' '\033]8;id=1;https://github.com\033\\test1\033]8;;\033\\\n' || exit 1
do_test '\033]8;;https://github.com/tmux/tmux\033\\test1\033]8;;\033\\\n' '\033]8;;https://github.com/tmux/tmux\033\\test1\033]8;;\033\\\n' || exit 1

$TERMO has 2>/dev/null && exit 1

exit 0
