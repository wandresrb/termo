# termo roadmap

termo is a fork of tmux with the relationship Neovim has to Vim: the proven
engine stays, the build, defaults, internals and extension model get modernised.
`docs/sdlc/` holds the intent, spec and plan for each phase; this file is the
map.

## Rules that hold in every phase

1. The regression suite in `tests/regress/` is green on every commit. A script
   only goes into `tests/regress/xfail` when the failure is upstream's and
   documented there.
2. Every build in CI runs under ASAN and UBSAN and is warning-free with
   `-Dwerror=true`.
3. Compiled-in defaults are tmux's. termo's opinionated defaults live in
   `etc/termo.conf`, installed as the system config, so the upstream tests keep
   their meaning and a user can see exactly what termo changes.
4. Hot paths (VT parsing, grid, redraw) never regress in throughput.
5. Extensibility is LuaJIT in-process or control mode out-of-process. No
   WebAssembly runtime, ever.
6. Upstream tmux fixes are cherry-picked per release into the parts that are
   still tmux (`input/`, `tty/`, `grid/`, `utf8/`). No full merges.

## Phase 1: build system and defaults (done, verified 2026-09-06)

- Meson + Ninja replacing autotools, C23 (`c_std=c23`), sanitizers as a build
  option, `src/` split by domain.
- Tests wired into `meson test`: Python integration suite, the full upstream
  regression suite run in parallel by `tests/regress/runner.py`, libFuzzer
  harnesses behind `-Dfuzz=enabled`.
- Rebranding: `termo.h`, `$TERMO`, `$TERMO_PANE`, `/tmp/termo-<uid>/`, with
  `$TMUX`/`$TMUX_PANE` kept for compatibility.
- `etc/termo.conf`: vi keys with `v`/`y`, 50k history, renumber-windows,
  focus events, OSC 52, 10 ms escape, RGB for every terminal.
- CI: Linux (gcc, clang) and macOS with sanitizers, nightly fuzzing and build
  variants, CodeQL, Scorecard, workflow linting, commit linting.

## Phase 2: unit test suite (tests landed 2026-09-07; plan `docs/sdlc/plan/003`)

Everything after this rewrites the leaf modules, and the only coverage they have is black-box.
One test binary, `tests/termo-test`, built from `tests/unit/*.c` against `libtermo` (the tree
minus `main.c`, archived so tests and fuzzers can link it). A 60-line `test.h` in C23:
`TEST(module, name)` self-registers with `[[gnu::constructor]]`, `CHECK_EQ` is `_Generic` over
the type, TAP output so Meson lists every case, no signal catching so a crash shows the sanitizer
stack. Files, in order of value: `compat` (the `strnvis` bug class), `options` and `cfg`
(the changed-defaults bug class, `etc/termo.conf` verified value by value), `format`
(division by zero), then `regsub`, `utf8`, `grid`, then `colour`, `style`, `key-string`,
`arguments`, `layout`, then `screen-write` and `input`: 14 modules, 155 cases, all in
`tests/unit/`. Coverage is measured nightly with gcovr; the phase closes when the report shows
80% lines in the leaf modules and 60% in `input.c`, `format.c`, `screen/write.c`.

## Phase 3: LuaJIT runtime and `termo.api` (plan `docs/sdlc/plan/002`)

Why Lua and not Rust, Go, JS or a data format is argued in the plan; the short form: the code
runs inside the server's single libevent thread on every keystroke and redraw, so it must be
in-process, tiny, embeddable through a C API, fault-isolated and editable without a compiler.
Lua is the only candidate that meets all of that, and it is what the target user already
writes for Neovim and WezTerm.

C side, in this order: `src/lua/runtime.c` (one `lua_State`, every entry through
`termo_lua_call` with an instruction budget and error routing), `run-lua`, `init.lua` loaded
at the tail of `start_cfg`, then `termo.api`: `eval` (the `#{}` format language as the
introspection API), `cmd` (commands with captured output, one sink added to `queue.c`),
`on/off/emit` (one Lua sink per name on the existing event bus), keymaps with functions
(`run-lua -r`), timers and `job_run`, menus and popups through their existing callbacks, and
Lua format variables registered through `format_add_cb` so the status line reads
`#{git_branch}` with no change to the format parser. `run-lua -j` returns JSON, which is how
Rust, Go, Python or an agent consume the same API out of process.

Lua side (`runtime/lua/termo/`), the Zellij-grade UX built on the API rather than in C:
`keymap`, `opt`, `ui`, `json`; then the fuzzy command palette (`src/core/fuzzy.c` + menus),
modal key tables with a hint bar, declarative layouts from Lua tables, default bindings for
the floating panes that already exist in the core, and `termopack` (git-based, `termo.json`
manifests) once the palette works.

Evals: `tests/lua/*_spec.lua` covering every function in `termo.api.list()`, ASAN clean
through `lua_close`, and regress unchanged with `-Dluajit=disabled`.

## Phase 4: C23 modernisation

Only changes with a payoff, each as its own PR with regress green:

1. `[[noreturn]]`, `[[maybe_unused]]`, `[[gnu::format]]` replacing the
   `__dead`/`__unused`/`printflike` macros; `compat.h` loses its attribute
   shims.
2. Prune `src/compat/` to what POSIX-2024 and the supported platforms lack, and
   `src/osdep/` to Linux, macOS, FreeBSD, OpenBSD, NetBSD.
3. `stdckdint.h` checked arithmetic in `grid/`, `screen/`, `utf8/` size
   computations.
4. `constexpr` and typed enums for the constants and flag sets in `termo.h`.
5. `-Wimplicit-fallthrough` with `[[fallthrough]]` in the state machines,
   `-Wconversion`, both under `-Werror` in CI.

Not in scope: splitting `termo.h`, replacing `cmd-parse.y` or the BSD
`queue.h`/`tree.h`, or a tree-wide `NULL` to `nullptr` sweep. They add churn
that breaks upstream cherry-picks without buying safety or speed.

Compiler floor from this phase on: clang 18, gcc 14.

## Phase 5: Rust in leaf modules

Strangler-fig, one module at a time, through Meson's native Rust support
(`rust_abi: 'c'`, no cargo, no crates): `regsub.c` as the pipeline proof, then
`utf8/`, `grid/`, `input/`. Each module keeps its C ABI from `termo.h`, shares
structs as `#[repr(C)]` with size checks on both sides, and stays behind a
build option until stable. The primary eval is differential fuzzing of the C
and Rust builds on the `tests/fuzz` corpus; the second is regress; the third is
the VT throughput benchmark, with a 10% regression as the gate.

`input.c` goes last: it is where terminal-emulator CVEs live and it only
moves after `grid/` is stable in Rust.
