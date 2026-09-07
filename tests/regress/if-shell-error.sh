#!/bin/sh

# 883
# if-shell with an error should not core :-)

PATH=/bin:/usr/bin
TERM=screen

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null

TMP=$(mktemp)
OUT=$(mktemp)
trap "rm -f $TMP $OUT" 0 1 15

cat <<EOF >$TMP
if 'true' 'wibble wobble'
EOF

$TERMO -f$TMP -C new <<EOF >$OUT
EOF
grep -q "^%config-error $TMP:1: $TMP:1: unknown command: wibble$" $OUT

cat <<EOF >$TMP
wibble wobble
EOF

echo "source $TMP" | $TERMO -C new  >$OUT
grep -q "^%config-error $TMP:1: unknown command: wibble$" $OUT
