# Intent 003: a real test suite before any further migration

- Status: Approved 2026-09-06
- Author: wandresrb

## Problem

termo has 129 upstream regression scripts and 5 integration tests, all black-box against the
binary, and one unit test that exercises `xmalloc`. Nothing looks inside a module. The bugs found
while closing phase 1 show what that costs: an ABI mismatch in `strnvis` on macOS crashed the
server on attach, changed compiled-in defaults broke four regression scripts, `format.c` cast
`inf` to `long long`, and `-DASAN` was never defined so sanitizer logs never existed. Each is a
one-line unit test that did not exist.

Phases 3 to 5 (LuaJIT, C23, Rust) rewrite exactly the modules with no coverage: `utf8/`, `grid/`,
`input/`, `format.c`, `options.c`. Doing that on top of a five-minute black-box suite is a
migration without a net.

## Outcome

A unit test suite in C, cmocka-based, linked against the whole tree as a static library so tests
use the real dependencies, one file per module, run by `meson test --suite unit` in seconds and in
every CI cell. Coverage is measured nightly with per-module minimums. Regression and integration
suites stay as the upper layers.

## Scope

`tests/unit/`, `tests/meson.build`, `meson.build` (`libtermo`), CI workflows, the four fuzz
harnesses (share the bootstrap), docs. No production code changes except what a test proves
wrong.

## Constraints

- Tests never start the real server; the harness sets up globals the way the fuzzers do.
- No sleeps, no sockets, no `/tmp` in unit tests.
- Every platform in CI builds and runs the suite: cmocka is a build dependency of the tests only.

## Open questions

None. Framework and linking model decided in `docs/sdlc/plan/003-test-suite.md`.
