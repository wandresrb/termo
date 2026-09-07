#!/bin/sh

# 882
# tmux inside if-shell itself should work

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

TMP=$(mktemp)
trap "rm -f $TMP" 0 1 15

cat <<EOF >$TMP
if '$TERMO run "true"' 'set -s @done yes'
EOF

TERM=xterm $TERMO -f$TMP new -d "$TERMO show -vs @done >>$TMP" || exit 1
sleep 1 && [ "$(tail -1 $TMP)" = "yes" ] || exit 1

$TERMO has 2>/dev/null && exit 1

exit 0
