# Intent 005: a CI gate that runs in minutes, and an e2e layer that is termo's

- Status: Approved 2026-09-08
- Author: wandresrb

## Problem

The first push of the fork opened PR #1 and four of five checks failed without the code being
at fault: Ubuntu's apt Meson is too old for `c_std=c23`, zizmor lost `contents: read`, a long
commit subject, and on macOS one upstream regress script died because its window runs `sleep
100` and the 3-core runner took 103 s to reach it. Behind that: six workflows with no `needs`,
no containers, every job installing its toolchain with apt or brew, `meson test` running all
four layers on three cells including macOS at 10x, and 262 s of upstream shell scripts with 551
`sleep`s on every PR. The 129 scripts under `tests/regress/` are upstream tmux's, none is
termo's; they synchronise with `sleep 1` and are not an integration suite.

## Outcome

A PR gate of lint, build, unit, Lua specs and e2e in a few minutes on Linux containers with
everything preinstalled, no macOS until the gate is clean. An e2e layer that belongs to termo:
pytest driving the binary through control mode and a pty, `capture-pane` as the screen oracle,
every wait a poll with a timeout, no `sleep`. Upstream's `regress/` stays available on demand
through `just upstream-regress` for releases and upstream cherry-picks, not maintained here.

## Scope

`.github/workflows/`, `ci/`, `tests/e2e/`, `tests/meson.build`, `meson.build`
(`meson_version`), `tools/regress-runner.py`, `justfile`, docs. `tests/regress/` and
`tests/integration/` are removed. No production code changes.

## Constraints

- Nothing installs packages inside a CI job; images are built by a workflow, never pushed
  from a laptop, so GHCR links them to the repository.
- Sanitised builds stay the rule for the gate; musl (no libasan) is portability coverage in
  nightly.
- No test waits with `sleep`; a failing wait prints its last observation.
- The suite runs and skips its Lua modules cleanly on a `-Dluajit=disabled` build.
