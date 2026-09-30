---
name: upstream-regress
description: Run upstream tmux's regress/ scripts (all, or the ones named) against build/termo and report failures with their logs.
---

Run `just upstream-regress $ARGUMENTS` from the repo root. It fetches
`upstream/master:regress/` into `build/upstream-regress/`, compiles if needed,
and runs `tools/regress-runner.py` in parallel; with no arguments that is all
129 scripts, several minutes under ASAN.

For a failure, read `build/upstream-regress/logs/<script>.log`, then rerun that
script alone with `sh -x` from `build/upstream-regress/regress` with
`TEST_TMUX=$PWD/../../termo` to see the exact command that failed. A server
crash leaves a report in `/tmp/termo-asan.*` or `/tmp/termo-ubsan.*`; read it
before guessing.

`tools/regress-xfail` lists scripts known to fail with a reason. Do not add to
it to make a run green; add only when the failure is upstream's and documented.
