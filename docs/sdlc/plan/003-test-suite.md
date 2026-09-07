# Plan 003: unit test suite

- Status: Approved 2026-09-06; Steps 1 to 4 landed 2026-09-07, coverage report pending on nightly
- Intent: [`intent/003-test-suite.md`](../intent/003-test-suite.md)
- Spec: [`specs/003-test-suite.md`](../specs/003-test-suite.md)

Four steps on the single working branch, each green on CI before the next. Proof for every step: `meson test -C build` passes
with sanitizers on the three CI cells, regress unchanged.

## Step 1: infrastructure and the modules that already bit us

- `meson.build`: `termo_lib`, `termo_exe` from `main.c` + `termo_lib`; `src/core/util.c` with
  the globals and helpers that were in `main.c`.
- `tests/unit/test.h`, `main.c`, `harness.c`: framework, TAP runner, bootstrap and reset.
- `tests/meson.build`: `termo-test` from `unit/*.c`, one `test()` per module; fuzzers use
  `harness.c`.
- `tests/fuzz/*-fuzzer.c`: `LLVMFuzzerInitialize` calls `termo_test_init`.
- `tests/unit/test_compat.c`, `test_options.c`, `test_cfg.c`, `test_format.c`.
- Delete `tests/unit/test_sanity.c`.
- `nightly.yml`: `coverage` job.
- `CLAUDE.md`, `README.md`, `CONTRIBUTING.md`, `ROADMAP.md`: phases renumbered, how to write a
  unit test, `/unit` skill.

Proof: `meson test --suite unit` shows every case (TAP); `test_cfg.c` loads `etc/termo.conf` and
checks each value; `test_compat.c` passes on macOS and Linux.

## Step 2: leaf modules that phases 4 and 5 rewrite

`test_regsub.c`, `test_utf8.c`, `test_grid.c`. Proof: nightly `no-utf8proc` variant runs
`test_utf8.c` and passes.

## Step 3: text parsers

`test_colour.c`, `test_style.c`, `test_key_string.c`, `test_arguments.c`, `test_layout.c`.

## Step 4: emulation

`test_screen_write.c`, `test_input.c`. Proof: DA reply case fails when built with `-Dsixel=true`
unless the expectation is sixel-aware (the test reads `ENABLE_SIXEL`).

## Close

Nightly coverage report meets the spec minimums. Then `docs/sdlc/plan/002-luajit.md` is signed
as phase 3.

## Risks

- Some `options.c` entry points call server-side functions (`recalculate_sizes`,
  `server_redraw_client`) on set. They are linked from `libtermo` and are no-ops without
  clients or sessions; if one dereferences a missing global, wrap it in that test.
- `load_cfg_from_buffer` queues commands through `cmdq`; the test must drain the queue with
  `cmdq_next(NULL)` as `input-fuzzer.c` does.
