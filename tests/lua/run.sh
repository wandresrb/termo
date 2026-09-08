#!/bin/sh
# Start a server on its own socket, run the Lua specs inside it, relay the
# TAP. run.lua collects every result; the report comes out once the last
# asynchronous spec has signalled wait-for.
set -u
TERMO=${1:?termo binary}
DIR=$(cd "$(dirname "$0")" && pwd)
SOCK=lua$$

TERMO_RUNTIME="$DIR/../../runtime"
export TERMO_RUNTIME

trap '"$TERMO" -L$SOCK kill-server 2>/dev/null' 0 1 15
"$TERMO" -L$SOCK -f/dev/null new-session -d -x 80 -y 24 || exit 1
"$TERMO" -L$SOCK run-lua -f "$DIR/run.lua" || exit 1
"$TERMO" -L$SOCK wait-for lua-specs || exit 1
report=$("$TERMO" -L$SOCK run-lua 'return spec_report()') || exit 1
printf '%s\n' "$report"
case "$report" in
*"spec(s) failed"*) exit 1 ;;
esac
exit 0
