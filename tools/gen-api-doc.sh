#!/bin/sh
# Regenerate docs/api.md: starts a throwaway server from the build tree,
# runs tools/gen-api-doc.lua in it and prints the result.
#
#   tools/gen-api-doc.sh > docs/api.md
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TERMO=${TERMO:-$ROOT/build/termo}
SOCK=apidoc$$

TERMO_RUNTIME=$ROOT/runtime
export TERMO_RUNTIME

trap '"$TERMO" -L$SOCK kill-server 2>/dev/null' 0
"$TERMO" -L$SOCK -f/dev/null new-session -d
"$TERMO" -L$SOCK run-lua -f "$ROOT/tools/gen-api-doc.lua"
