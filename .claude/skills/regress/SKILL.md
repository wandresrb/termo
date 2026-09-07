---
name: regress
description: Run the tmux regression suite (all scripts, or the ones named) against build/termo and report failures with their logs.
---

Run `python3 tests/regress/runner.py build/termo $ARGUMENTS` from the repo root
(`ninja -C build` first if sources changed). With no arguments it runs all 129
scripts in parallel, about 4 minutes under ASAN.

For a failure, read `tests/regress/logs/<script>.log`, then rerun that script
alone with `sh -x` from `tests/regress` with `TEST_TERMO=$PWD/../../build/termo`
to see the exact command that failed. A server crash leaves a report in
`/tmp/termo-asan.*` or `/tmp/termo-ubsan.*`; read it before guessing.

`tests/regress/xfail` lists scripts known to fail with a reason. Do not add to
it to make a run green; add only when the failure is upstream's and documented.
