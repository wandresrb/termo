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
`arguments`, `layout`, then `screen-write` and `input`: 14 modules, 281 cases (291 with the
`lua` module), all in `tests/unit/`. The phase closes on the spec's "Must cover" table: every row
has named cases that prove it (plan 003, Step 5). Coverage is measured locally as information to
find untested paths; it is not a gate and does not run in CI.

## Phase 3: C23 and POSIX.1-2024 (plan `docs/sdlc/plan/004`)

Done before Lua so new code is born in the final style and the warning floor rises on a
quiet tree. Compiler floor: GCC 14, Clang 20, Apple clang 21, enforced at `meson setup`.

1. Attributes: `__dead`/`__unused`/`printflike`/`FALLTHROUGH` comments become
   `[[noreturn]]`, `[[maybe_unused]]`, `[[gnu::format]]`, `[[fallthrough]]`;
   `compat.h` loses its attribute shims; `-Wimplicit-fallthrough` on.
2. Platforms and compat, measured against POSIX.1-2024: `osdep/` keeps Linux, macOS,
   FreeBSD, OpenBSD, NetBSD; `compat/` keeps only what a target lacks (macOS: `reallocarray`,
   `closefrom`, `explicit_bzero`; glibc: `getprogname`, `strtonum`, `b64`); every `HAVE_*`
   the code reads is defined by a Meson probe or deleted. `systemd` option wired.
3. Warnings measured then enforced: `-Wshadow`, `-Wmissing-prototypes`, `-Wstrict-prototypes`,
   `-Wvla`, `-Wformat=2` under `-Werror`; `-Wconversion` recorded, not forced.
4. `<stdckdint.h>` on the 25 size computations in `grid/`, `screen/`, `utf8/`, `input/`.
5. `termo.h` typed: `static inline` for function-like macros, fixed-type enums for flag
   groups with `static_assert` on size, `constexpr` for constants.
6. `.clang-tidy` with a baseline, and the new-code rules in `CLAUDE.md`.

Not in scope: `NULL` to `nullptr` sweeps, `u_int` to `uint32_t`, replacing `queue.h`,
`tree.h`, `cmd-parse.y`, `gettimeofday` or `ioctl`; `<stdbit.h>`, `memset_explicit`,
`#embed`, `char8_t` (missing on a target or on GCC 14).

## Phase 4: LuaJIT runtime and `termo.api` (plan `docs/sdlc/plan/002`)

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
