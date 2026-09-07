#!/bin/sh
# Start a server on its own socket, run the Lua specs inside it, relay the TAP.
set -u
TERMO=${1:?termo binary}
DIR=$(cd "$(dirname "$0")" && pwd)
SOCK=lua$$

TERMO_RUNTIME="$DIR/../../runtime"
export TERMO_RUNTIME

trap '"$TERMO" -L$SOCK kill-server 2>/dev/null' 0 1 15
"$TERMO" -L$SOCK -f/dev/null new-session -d -x 80 -y 24 || exit 1
"$TERMO" -L$SOCK run-lua -f "$DIR/run.lua"
