# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`termo` is a fork of tmux (the Neovim-to-Vim relationship): the tmux C client/server core, libevent loop and VT emulation are kept intact; on top go sane defaults, embedded LuaJIT scripting, a manifest-driven plugin system (`termo.json` + `termopack`), and Zellij-style UX. `ROADMAP.md` is the source of truth for phases and the hard rules (no WASM, regress always green, sanitizer clean). Phase 1 (Meson build, rebranding, defaults, CI) is done; Phase 2 (unit test suite, `docs/sdlc/plan/003-test-suite.md`) has its 14 modules landed; Phase 3 (C23 and POSIX.1-2024, `docs/sdlc/plan/004-c23-posix.md`) is in progress; Phase 4 (LuaJIT, spec `docs/sdlc/specs/002-luajit-runtime.md`) follows it. Phase 4 step 1 is in: `src/lua/runtime.c` (one `lua_State`, `termo_lua_call` with a wall-time budget and error routing, JIT engine off because LuaJIT count hooks never fire inside a compiled trace, and `jit.on`, `debug.sethook`, `os.execute`, `io.popen`, `os.exit` removed so the budget has no off switch), `src/lua/api.c` (`termo.api.version/list/eval/get_option/set_option`), `src/cmd/run-lua.c`, and `~/.config/termo/init.lua` as the last entry of the `TMUX_CONF` search list (`load_cfg` queues `.lua` files so they run after the commands before them; `-f` replaces the list, so tests with `-f /dev/null` never see it). Everything is behind `HAVE_LUAJIT`; `-Dluajit=disabled` must keep building and passing. `runtime/lua/termo/` is the Lua side, installed to `<datadir>/termo/runtime` and found through `TERMO_RUNTIME` in development (`tests/lua/run.sh` exports it).

Two remotes: `origin` is this fork, `upstream` is `tmux/tmux` for pulling fixes into the C parts that are still tmux.

## SDLC process

Non-trivial work follows `docs/sdlc/`: `intent/NNN-*.md` (what and why) → `specs/NNN-*.md` (technical design) → `plan/NNN-*.md` (exact files, order, risks, which eval proves each step). A plan needs `Status: Approved` before implementation code is written; `/sdlc-plan NNN` drives that. PRs link all three (see `.github/PULL_REQUEST_TEMPLATE.md`). Commits are one line, no body.

## Build

```sh
meson setup build -Db_sanitize=address,undefined -Dbuildtype=debugoptimized
ninja -C build
./build/termo -V
```

Compiler floor: GCC 14, Clang 20, Apple clang 21 (Xcode 26); `meson setup` probes `nullptr`, `constexpr`, fixed-type enums and `<stdckdint.h>` and stops with a message otherwise. Meson options (`meson_options.txt`): `utf8proc`, `luajit`, `systemd` (features, default `auto`), `sixel` (bool, default off, like upstream: it changes the DA reply that regress checks), `fuzz` (feature, needs clang with libFuzzer). The build defines `-DDEBUG` unconditionally, defines `ASAN` when `b_sanitize` includes address (that is what enables the sanitizer option strings in `main.c`, logs go to `/tmp/termo-asan.*` and `/tmp/termo-ubsan.*`), and passes `warning_level=2`; CI adds `-Dwerror=true`, keep it warning-free.

Compiled-in defaults are tmux's. termo's defaults (vi keys, 50k history, renumber, focus events, RGB) are `etc/termo.conf`, installed to `<sysconfdir>/termo/termo.conf` and first in the `TMUX_CONF` search path. Tests run with `-f/dev/null` so they see upstream semantics; change a default there, not in `options-table.c`. `src/cmd/cmd-parse.y` is compiled with bison into `build/cmd-parse.c`. Adding a `.c` file means adding it to the right `*_sources` list in `meson.build`; only the `osdep-<platform>.c` matching the host is compiled.

## Tests

```sh
meson test -C build --print-errorlogs      # unit + integration + full regress, ~5 min under ASAN
meson test -C build --suite unit           # every C unit case, TAP, seconds
./build/tests/termo-test format expressions   # one module, or one case by substring
meson test -C build --suite integration    # tests/integration/test_termo.py
meson test -C build --suite regress        # all 129 tests/regress/*.sh via runner.py, in parallel
python3 tests/regress/runner.py build/termo alerts.sh   # one or more scripts, .sh optional
```

**Unit tests** (`tests/unit/`): one binary, `termo-test`, linking the whole tree as `libtermo`
(every object except `main.c`, archived so tests and fuzzers can link it; not an installed library).
`test.h` is the framework: `TEST(module, name)` registers itself, `CHECK_EQ(a, b)` is `_Generic`
over the type, `CHECK*` continue and `REQUIRE*` return. No signal catching: a crash prints the
sanitizer stack. `TERMO_TEST_LOG=1` writes the server debug log to `termo-test-<pid>.log`. `harness.c` sets up the server globals without a server and resets the option
trees before each case. A new test file goes in `unit_sources` and the module list in
`tests/meson.build`. A change in a leaf module (`utf8/`, `grid/`, `input/`, `format.c`,
`options.c`, `cfg.c`, `compat/`) comes with its unit test. Process globals live in
`src/core/util.c`; do not add new global state.

**Regress** (`tests/regress/runner.py`) runs the upstream scripts with `-j` workers (default: CPU
count), forces `SHELL=/bin/sh` (a title-setting user shell renames windows under the tests), writes
the output of failures to `tests/regress/logs/<script>.log`, and reads `tests/regress/xfail` for
scripts expected to fail with their reason. Each script spawns its own server on
`-LtestA$$ -f/dev/null`; a script needs a `$$` socket name or it collides in parallel. A server
crash under sanitizers leaves `/tmp/termo-asan.*` or `/tmp/termo-ubsan.*`, and macOS writes a
crash report under `~/Library/Logs/DiagnosticReports/termo-*.ips`; read those before guessing.

**Lua specs** (`tests/lua/`): `meson test -C build --suite lua` runs `tests/lua/run.sh`, which starts a server on its own socket and executes `run.lua` through `run-lua -f`; specs use `describe/it/eq/fails` and print TAP back to the client. A new spec file goes in the list at the bottom of `run.lua`. `tests/unit/test_lua.c` covers the runtime without a server.

**Fuzzers**: `meson setup build -Dfuzz=enabled` (clang with libFuzzer, not Apple clang) builds
`tests/<target>-fuzzer` for `cmd-parse`, `format`, `input`, `style`, with seed corpora in
`tests/fuzz/corpus/`; `meson test --suite fuzz` is a short smoke run, nightly CI runs them for 10
minutes each.

**Coverage** is measured locally, never as a CI gate: `brew install gcovr`, `meson setup build-cov
-Db_coverage=true -Db_sanitize=none -Dbuildtype=debug`, `meson test -C build-cov --suite unit`,
`ninja -C build-cov coverage-text`, report in `build-cov/meson-logs/coverage.txt`. The Phase 2
close criterion is the "Must cover" table in `docs/sdlc/specs/003-test-suite.md`, not a percentage.

## C23 conventions for new code

The tree is C23 and CI compiles with `-Werror` plus `-Wshadow -Wmissing-prototypes
-Wstrict-prototypes -Wvla -Wformat=2 -Wsign-compare -Wimplicit-fallthrough`. New code uses
`nullptr` (not `NULL`), `bool`, `constexpr` for typed constants (not `#define`), fixed-type
enums for flag sets, `[[nodiscard]]` on functions returning resources or error codes,
`[[maybe_unused]]`, `[[noreturn]]`, `[[fallthrough]];`, `[[gnu::format(printf, a, b)]]`, and
`ckd_add`/`ckd_mul` from `<stdckdint.h>` for size arithmetic. No direct `__attribute__`, no
VLAs, no new `HAVE_*` without a Meson probe, nothing new in `src/compat/` unless a target
platform (Linux glibc/musl, macOS, FreeBSD, OpenBSD, NetBSD) lacks it. Existing code is not
rewritten for style: no `NULL` to `nullptr` sweeps. `.clang-tidy` lists the checks nightly
enforces against `tools/clang-tidy-baseline`.

## Source layout

`src/` is split by domain, not one flat directory like upstream. When pulling from `upstream`, the old path is `<name>.c` at the root and the new one is usually `src/<domain>/<name-without-prefix>.c` (e.g. `cmd-new-window.c` → `src/cmd/window/new.c`, `window-copy.c` → `src/window/copy.c`, `tty-keys.c` → `src/tty/keys.c`, `grid-view.c` → `src/grid/view.c`). The master header is `src/core/termo.h` (all structs, all prototypes); `tmux.h` and `tmux-protocol.h` are forwarding shims kept for upstream diffs.

## Architecture

One binary is both **client and server**: `src/core/main.c` decides which at startup based on whether the socket is already listening. The server (`src/server/`) owns all state; clients (`src/client/`) are thin, they send commands over the Unix socket (`imsg` in `src/compat/`) and draw what the server tells them.

**Object graph** (all in `termo.h`): `client` (attached tty, current session, key tables) → `session` (named set of windows plus navigation history) → `winlink` (a session's index into a `window`; windows can be linked into several sessions) → `window` (pane set plus `layout_cell` tree) → `window_pane` (one pty, its process, its `screen`) → `screen` → `grid` of `grid_cell` (buffer plus scrollback). Ownership is shared and mutable via intrusive `TAILQ`/`RB_HEAD` lists.

**Command pipeline**: `src/cmd/cmd-parse.y` turns text (config files, `bind-key`, CLI) into a `cmd_list` of `cmd`s, each backed by a `cmd_entry` in a `src/cmd/**/*.c` file. Commands are never run directly: `src/cmd/queue.c` pushes `cmdq_item`s onto a per-client (or detached "state") queue and runs them asynchronously so a command can wait on jobs, prompts or confirmations. `src/cmd/find.c` resolves `-t` targets. A new command means a new file with a `cmd_*_entry` and `cmd_*_exec`, plus registration in the `cmd_table` in `src/cmd/cmd.c`.

**Terminal I/O**: `src/input/input.c` is the VT100/xterm parser turning pane pty output into `screen-write` calls (`src/screen/write.c`, the shared drawing API also used for status line, menus, copy mode). The reverse direction is `src/tty/`: `tty.c`/`draw.c` render the server's screens to the outer terminal via terminfo (`term.c`, `features.c` for capability detection such as RGB), `keys.c` decodes raw keys from the client terminal into `key_code`s.

**Options**: `src/config/options.c` stores every option in trees at global, session and window/pane scope with parent fallback; the schema and upstream defaults live in `src/config/options-table.c`, default key bindings in `src/core/key-bindings.c`. Both stay identical to tmux; termo's changes are `etc/termo.conf`. Config search order is `<sysconfdir>/termo/termo.conf`, `~/.config/termo/termo.conf`, `~/.config/tmux/tmux.conf`, `~/.tmux.conf` (`TMUX_CONF` in `meson.build`).

**Formats**: the `#{...}` language is `src/format/format.c` (variables, conditionals, modifiers) and `src/format/draw.c` (styled/aligned rendering for status line and borders).

**Hooks and events**: `src/core/hooks.c` (`set-hook`) sits on `src/core/events.c` + `events-payload.c`, the pub/sub that also feeds control-mode notifications (`src/core/control.c`, `control-notify.c`).

**Modes**: interactive overlays (copy mode, choose-tree, buffer/client/window pickers, customize) are `window_mode`s in `src/window/`, sharing the generic list UI in `src/core/mode-tree.c`. Popups and menus are `src/core/popup.c` / `menu.c`. Floating panes already exist in the core (`layout_floating_*`, `window_pane_is_floating`); Phase 4 exposes them from Lua rather than adding a new pane kind.

**Environment**: panes get `TERMO`, `TERMO_PANE`, `TERM_PROGRAM=termo`, and for compatibility `TMUX`/`TMUX_PANE`. The socket dir is `/tmp/termo-<uid>/` (override `TERMO_TMPDIR`).

**Portability**: targets are Linux (glibc or musl), macOS, FreeBSD, OpenBSD, NetBSD; `meson setup` errors on anything else. `src/compat/` holds only what a target lacks (macOS: `reallocarray`, `closefrom`, `explicit_bzero`, `htonll`; glibc: `getprogname`, `strtonum`, `setproctitle`, `b64_*`; plus `vis`, `imsg`, BSD `getopt`, `fdforkpty`), each behind a Meson probe; every `HAVE_*` the code reads is defined by Meson or does not exist. `src/osdep/` has one file per target; see `ROADMAP.md` Phase 5 for the Rust leaf-module plan (`regsub` → `utf8` → `grid` → `input`).

## Repo conventions

- `.github/copilot-instructions.md`: no PR review comments from bots in this repo.
- CI (`.github/workflows/`): `ci.yml` is the merge gate (Linux gcc/clang + macOS, ASAN+UBSAN, `-Dwerror`, full `meson test`); `nightly.yml` runs fuzzing, build variants (`-Dluajit=disabled`, `-Dutf8proc=disabled`, sixel), clang-tidy and FreeBSD; `lintcommit.yml` enforces one-line commits; `codeql.yml`, `scorecard.yml`, `zizmor.yml` are the security scans.
- `.claude/settings.json` rebuilds with ninja after every edit under `src/` and feeds compiler errors back; `.claude/skills/` has `/unit`, `/regress` and `/sdlc-plan`.
- Docs that moved: man page is `docs/man/termo.1`, changelog `docs/CHANGES`, upstream sync notes `docs/SYNCING.md`, example config `docs/example_termo.conf`.
