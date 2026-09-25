# Plan 006: Rust where the bytes are untrusted, measured before moved

- Status: Approved 2026-09-14; amended 2026-09-15 (Track B rows 10, 12, 15, new 22 to 26; see Amendment)
- Intent: [`intent/006-rust.md`](../intent/006-rust.md)
- Spec: [`specs/006-rust.md`](../specs/006-rust.md)

Steps land as merges into `refactor/to-termo`, no pull requests: Track A works in the
worktree `../termo-rust` on branch `rust/006`, Track B in `../termo-product` on
`product/006`, each merged into `refactor/to-termo` when a step is green locally
(`meson test` with and without `-Drust`, `just bench --compare` where the step touches a
hot path) and rebased on it before starting the next; CI runs on the push. Steps 1 to 3 are the prerequisites of everything; 4 to 9 are the
Rust modules in the order decided (`utf8/` → `grid/`); 10 to 17 and 22 to 26 are the
product axes, Lua-first, and can interleave with 5 to 9 because they touch different
files; 18 to 21 are the auxiliary executables, last. `input/` in Rust is not a step of this plan: it gets its
own plan step appended when step 9 is closed and step 2's numbers say what the parser
stage costs.

| Step | Contents | Proof |
|---|---|---|
| 0 | These three documents; `ROADMAP.md` Phase 5 and rules 6 and 7 | — |
| 1 | **Benchmark.** `tools/bench/bench.py` (imports `Server`, `Pty`, `expect` from `tests/e2e/termo.py`), `tools/bench/scenarios/{ascii,utf8,sgr,scroll,resize,cursor,sync}/{setup,benchmark}` (vtebench's generators vendored as Python), payload cache in `build/bench/payloads/`, two measurements per scenario (pty client attached, no client), JSON per git sha in `build/bench/`, `--compare <sha>` with the 10% rule, `just bench [scenario]` and `just bench-compare <sha>`; `docs/bench.md` | `just bench` writes the JSON with 7 scenarios × 2 measurements on macOS and in the Ubuntu image; `--compare` against a copy with one number lowered by 12% exits 1 and names the scenario; the first baseline sha is recorded in `docs/bench.md` |
| 2 | **C fixes measured.** One commit each, before/after ratio in the subject line's PR description: `bufferevent_set_max_single_read(wp->event, 65536)` in `window_pane_set_event` plus a per-iteration parse budget in `input_parse_pane` (`input_parse_buffer` stops after N bytes and re-arms through the existing `EV_READ` path; N from step 1); `grid_scroll_history` growing `linedata` geometrically with a `linealloc` field; `since_ground` capped at `input_buffer_size` (`input_parse` drops to ground with a log line when reached); `EV_READ` high watermark of 65536 on the control client's `read_event` | unit: `test_input.c` cases for the parse budget resuming mid-sequence and for the `since_ground` cap; `test_grid.c` case for growth without per-line `realloc` (count allocations through `--wrap=xreallocarray`); e2e `test_control.py` case: a 128 KiB line without `\n` disconnects the client cleanly; `just bench --compare` shows the ratio per fix; regress green |
| 3 | **Rust toolchain in the tree.** `meson_options.txt` `rust` (feature, auto); `meson.build`: `add_languages('rust')`, the 1.98 probe with its message, `rust_std=2024`, `rs_args` (`-Cpanic=abort -Coverflow-checks=on`, `-D warnings` from `werror`), `HAVE_RUST`; `src/rs/{lib.rs,sys.rs,ffi/mod.rs,ffi/version.rs,Cargo.toml,cbindgen.toml,rustfmt.toml}` with `#![forbid(unsafe_code)]` and the lint set, exporting `termo_rs_version()`; `rust.cbindgen` → `termo_rs.h`; `getversion()` appends `+rust`; `termo_lib` links `termo_rs`; `ci/Dockerfile.ubuntu` and `ci/Dockerfile.alpine`: rustup 1.98.0 with `clippy`, `rustfmt`, `rust-src`, `bindgen-cli 0.73.2`, `cbindgen`, plus nightly with `miri` (Ubuntu only); FreeBSD `prepare` installs rustup; `ci.yml`: `-Drust=enabled` in configure, `clang-tidy` job renamed `lint` and gaining `cargo fmt --check` and `cargo clippy --all-targets -- -D warnings` in `src/rs/`; `nightly.yml`: `rust-verify` job (`cargo +nightly miri test`), `variants` gains `no-rust`; `docs/crates.md` (empty allowlist), `docs/ci.md`, `CLAUDE.md` Rust section; `justfile` `rs-lint` | `meson setup -Drust=enabled` fails below 1.98 with the message (tested with `rustup run 1.97.1`); `./build/termo -V` prints `+rust`; gate green on gcc-14, clang-20, macOS with `-Drust=enabled`; nightly green on `no-rust`, Alpine, FreeBSD; `lint` red on a deliberate `unsafe {}` without `SAFETY:` (verified once, reverted) |
| 4 | **bindgen and vendored crates.** `src/rs/bindings/wrapper.h` (includes `termo.h`, `#define` mirrors for the `constexpr` constants Rust reads), `rust.bindgen` with `output_inline_wrapper`, `bindings_inline.c` into `termo_lib`, `structured_sources` for the generated file; `subprojects/` wraps for `libc`, `memchr`, `unicode-width`, `unicode-segmentation`, `lz4_flex` (each with a `meson.build` building `rust_abi: 'rust'`), `rust_dependency_map`, `Cargo.toml` `path` deps to the same sources; `docs/crates.md` filled (version, licence, review note, commit) | `libtermo_rs.a` links and `cargo clippy` in `src/rs/` sees the same crates; the `#[repr(C)]` mirrors of `utf8_data`, `grid_cell`, `grid_cell_entry`, `grid_extd_entry` compile with their `const` size asserts and the matching `static_assert`s in `termo.h`; bindgen on the C23 header produces the expected output for `nullptr`, `bool`, fixed enums and the mirrored constants (differences noted in the PR) |
| 5 | **`utf8/` in Rust, ABI parity.** `src/rs/utf8/{decode,width,pack,vis,cstr,combined}.rs`, `src/rs/ffi/utf8.rs` exporting the 30 functions; `libc::malloc` for returned buffers; the `codepoint-widths` cache and the 160-entry default table ported verbatim; `unicode-width` behind the cache; interning as `Vec` + `HashMap` under a `Mutex`; the two `fatalx` become `UTF8_ERROR`; C `utf8/` kept, compiled into `libtermo_c_utf8` with `-DTERMO_C_PREFIX=termo_c_` (a macro renaming its exports) for the fuzzer; `tests/fuzz/utf8-fuzzer.c` (differential: decoder state, widths, packing round trip, `utf8_sanitize`, `utf8_strvis`) with corpus and dict; `#[test]`s in each Rust file; `rust.test('rs', ...)` in `tests/meson.build`; `-Dutf8proc` no longer affects the Rust build | `tests/unit/test_utf8.c` unchanged and green against Rust; `meson test --suite fuzz` runs `fuzz_utf8` with no divergence; 10-minute nightly run clean; `just bench --compare` within 10% on `utf8` and `ascii`; regress green with and without Rust; `cargo +nightly miri test` green |
| 6 | **Widths at Unicode 17 and mode 2027.** `unicode-width-cjk` server option (`options-table.c`, default off); `MODE_GRAPHEMES` screen mode, `DECSET/DECRST/DECRQM 2027` in `input.c`; general UAX #29 combining in `utf8_should_combine` when the mode is on (`GraphemeCursor` over the previous cell plus the new codepoint, width of the cluster from `unicode-width`); `tty/features.c` `graphemes` feature from the `CSI ? 2027 $ p` reply at attach (`tty/keys.c` parses `CSI ? 2027 ; Ps $ y`), `tty.c` sends `DECSET 2027` to such clients when any pane has the mode; `docs/man/termo.1`; `docs/SYNCING.md` entry | unit: `test_utf8.c` cases for a ZWJ chain of three, a keycap and an Indic conjunct combining only with the mode on; `test_input.c` case for `DECRQM 2027`; e2e `test_screen.py`: fixture emits `DECSET 2027` and `👨‍👩‍👧`, `capture-pane -e` shows one cell of width 2 and `CSI 6 n` reports the cursor two columns on; same bytes with the mode off give today's cells; e2e `test_control.py`: `#{client_termfeatures}` contains `graphemes` when the pty answers the query |
| 7 | **Grid encapsulation in C.** Accessors in `grid.c` and `termo.h` (`grid_line_flags`, `grid_line_set_flags`, `grid_line_clear_flags`, `grid_line_osc133`, `grid_line_cellsize`, `grid_line_cellused`, `grid_line_time`, `grid_hsize`, `grid_hlimit`, `grid_hscrolled`, `grid_scroll_added`, `grid_lines_total`); every field access in `window/copy.c`, `cmd/pane/capture.c`, `screen/write.c`, `format/format.c`, `screen/screen.c`, `input/input.c`, `core/session.c`, `tty/draw.c`, `window/panes.c`, `cmd/pane/resize.c` rewritten to them; `struct grid` and `struct grid_line` definitions move to `src/grid/grid.h`, forward-declared in `termo.h`; `docs/SYNCING.md` records the divergence and the porting rule | `grep -rn -- '->linedata\|->celldata\|->extddata\|->cellsize\|->cellused\|->extdsize\|->hsize\|->hlimit\|->hscrolled\|osc133_data' src --exclude-dir=grid` is empty; `tests/unit/*` unchanged and green; regress green; `just bench --compare` unchanged (accessors are `static inline` where the compiler must see through them) |
| 8 | **`grid/` in Rust.** `src/rs/grid/{cell,line,page,pool,history,compress,marks,reflow,string}.rs`, `src/rs/ffi/grid.rs` exporting the `grid.c` ABI (the 70 functions minus `reader.c` and `view.c`, which stay C over the ABI); pages of 64 KiB from a pool, line index with `LineRef`, extended entries bump-allocated in the page; idle compression with `lz4_flex` behind a C-side `evtimer` armed through a registered callback (`grid_set_idle_hook` in `screen.c`), 250 ms debounce, transparent inflate on access, `madvise` on Linux/macOS (`libc`), plain free elsewhere; `hlimit` trimming by page; mark ids on prompt lines, `grid_marks_next/prev`, `grid_mark_range`, placement slots without a consumer; C `grid.c` kept as `libtermo_c_grid` for the fuzzer; `tests/fuzz/grid-fuzzer.c` (differential over a screen-write action stream: `grid_string_cells` per line, `grid_compare`, reflow through ten widths) with corpus recorded from the e2e runs by a `TERMO_RECORD_ACTIONS` env hook in `screen/write.c` (debug builds only) | `tests/unit/test_grid.c` unchanged and green; `fuzz_grid` no divergence in 2000 runs and 10 nightly minutes; `just bench --compare` within 10% on all scenarios; peak RSS after the `utf8` payload at 50k lines below 40% of the C build's (the number in the PR); regress green; `test_input.c` and `test_screen_write.c` unchanged |
| 9 | **Remove the C twins.** After one tagged release with both: delete `src/utf8/*.c`, `src/grid/grid.c`, the `libtermo_c_*` targets and the `TERMO_C_PREFIX` macro; `-Drust` loses `auto` (required); `-Dutf8proc` removed; the fuzzers keep running the Rust side against the unit oracle (`grid_string_cells` snapshots in `tests/fuzz/corpus/grid/*.expected`); `docs/SYNCING.md` porting rule active | `meson test` green on every cell with `-Drust=enabled`; `nightly` `no-rust` variant removed; the release notes name the removal |
| 10 | **Modes and user commands.** C: `client-key-table-changed` fired from `server_client_set_key_table` with the table name, hook entry in `options-table.c`; `PROMPT_COMPLETE_LUA` hook in `prompt.c` calling `termo_lua_complete(prefix)` for unknown command names. Lua: `runtime/lua/termo/mode.lua` (replaces `hints.lua`, `termo.hints` kept as an alias for one release), `command.lua` (`termo.command.define`, `command-alias` registration, completion), `@modes` gate and `@hints` option (`always` keeps the second status row while modes are on, `mode` shows it only inside a mode), every mode table binds `Any` to itself, the prefix reset is treated like `Escape`, locked is `prefix None` plus a root binding (session scope, no C); the runtime ships insert, normal and locked, `etc/termo.conf` unchanged, `docs/example_init.lua` turns modes on and defines pane, window, session and resize sub-modes as the example; `termo.api.list()` gains `complete` | Lua specs `mode_spec.lua`, `command_spec.lua` (define, run with args from `command-prompt`, completion list); e2e `test_lua_ui.py`: leader enters a mode, the hint bar shows its keys, an unbound key is swallowed, a `bind -n` root key does not fire inside a mode, `Escape` leaves, the bar stays with `@hints=always` and disappears with `mode`, `:c2p 2` runs a defined command with an argument; `test_control.py`: `%client-key-table-changed` notification on `switch-client -T`; `docs/api.md` regenerated |
| 11 | **Copy mode: text objects and prompt jumps.** `window/copy.c` `-X` commands `select-word-inner/outer`, `select-quote-inner/outer`, `select-bracket-inner/outer`, `select-paragraph`, `next-prompt`, `previous-prompt`, `select-command-output`, `goto-mark`; `capture-pane -M <mark>`; `etc/termo.conf` bindings (`iw aw i" a" i( a( ip [[ ]] gO`); `docs/man/termo.1`; `docs/SYNCING.md` | unit `test_screen_write.c`/`test_grid.c` cases for the mark queries; e2e `test_screen.py`: a fixture shell with OSC 133 runs three commands, `[[`/`]]` land on the prompts, `gO` selects the last output, `capture-pane -M` returns it; regress green |
| 12a | **`termopack`, `vim.pack` parity** (spec §8.3 phase 1). `pack.lua`: `setup(specs, { load })` with spec `{src, name, version, data, dependencies}`, semver ranges over `git tag`, clone with `--filter=blob:none`, `termo-pack-lock.json` in the config dir read first and written after every change, install confirmed with `termo.ui.confirm` on the starting client or on the first `client-attached`, `update()` with a `termo.ui.menu` per plugin showing `git log --oneline old..new`, `del`, `clean`, `get` with `load_ms`, dependencies loaded first, `@pack-changed-pre`/`@pack-changed`, manifest `version`, `dependencies`, `lib` (`ffi.load`), and the phase-2 fields parsed; `docs/plugins.md` (schema, lockfile, native plugins and the trust model); `health.lua` lists inactive plugins | Lua spec `pack_spec.lua` against local bare git repos created in the spec (tags `v1.2.0`, `v2.0.0`): range `1.x` picks `v1.2.0`, lockfile round trip installs the same rev on a fresh data dir, install and `update` ask and apply on confirm, `update` lists the pending commits, `del` removes and `clean` removes only inactive ones, a dependency loads before its dependant and a cycle errors, events fire with `kind`; e2e: a plugin with a `lib` built from `tests/e2e/fixtures/plugin_lib.c` (C, not Rust, to keep the test toolchain small) is loaded through `ffi` |
| 12b | **Lazy layer** (spec §8.3 phase 2). In `pack.lua` (its own file only past ~150 lines): a spec with triggers loads with `load = false` and registers stubs; `termo.pack.load(name)` idempotent; triggers `event`, `keys`, `cmd` (needs row 10's `termo.command`), `cond`, `lazy`, `mode`, `format`; manifest `commands = {{name, desc, nargs}}` and `keys = {{table, key, command}}` carry the action so a stub loads and runs it, no key re-feed; a plugin is lazy iff it has triggers, spec over manifest | `pack_spec.lua`: a `keys` stub loads and runs the declared command, an `event` trigger loads once, `cond = false` stays inactive, `mode` loads on `client-key-table-changed`, `format` loads on the first `#{x}` evaluation, a manifest command appears in `termo.command` and a manifest key is bound; `load_ms` reported by `get` |
| 13 | **Agent-aware core.** `agent-idle-time` option; `pane_command_state` format (`idle|running|waiting|finished`) from `PANE_CMDRUNNING`, output generation and a per-pane idle timer; `pane-waiting-for-input` event; `OSC 9;4` progress states mapped to the same; `termo.api.notify(text[, client])` → OSC 9 / OSC 777 by a `notify` terminal feature (`tty/features.c`, on for `ghostty`, `WezTerm`, `kitty`, `foot`, `iTerm2`); `docs/man/termo.1` | unit `test_input.c`: OSC 9;4 states; e2e `test_control.py`: a pane running `sleep 5` shows `running`, `read` shows `waiting` after `agent-idle-time`, `%pane-waiting-for-input` arrives; Lua spec for `notify`; e2e on a pty: the OSC 9 bytes reach the client |
| 14 | **Sandboxed panes.** `sandbox-profile[]` option and `termo.sandbox.profile()`; `-S <profile>` on `new-window`, `split-window`, `new-pane`, `respawn-pane`, `respawn-window`; `termo_sandbox_apply()` in `src/rs/ffi/sandbox.rs` (Landlock via the `landlock` crate and `seccompiler`, both added to the allowlist; macOS `sandbox_init` through `src/compat/sandbox-darwin.c`; OpenBSD `unveil`; no-op with cause elsewhere) called in `spawn_pane` after the cgroup move; `env:scrub`; `#{pane_sandbox}`; `docs/man/termo.1` | e2e `test_session.py` (Linux only, skipped elsewhere): a `-S readonly` pane cannot `touch` outside `cwd` and cannot `curl` when `net:off`, `#{pane_sandbox}` names the profile, an `AWS_SECRET_ACCESS_KEY` in the server environment is absent in the pane; unit for the profile parser; macOS e2e for `fs:ro` only |
| 15 | **Health, Neovim, sessionizer.** `health.lua` (`termo health` user command and palette entry, `#{client_termfeatures}`, clipboard options, budget, plugins with revs, lock state and inactive ones, the `resurrect` mode, shell integration state of the current pane, sandbox support, `+rust`), `nvim.lua` (`navigate`, `open`, `scrollback.edit()` in a float), `sessionizer.lua`; the `termo.nvim` plugin skeleton in a sibling repository named in `docs/plugins.md`. Sessions moved to row 22 | Lua specs for each module (`health_spec` output contains every section); e2e `test_lua_ui.py`: sessionizer prompt creates a session for a picked directory; `scrollback.edit` opens a float running `nvim` when present (skipped otherwise) |
| 16 | **`nucleo` behind `fuzzy_match`.** `nucleo-matcher` vendored; `src/rs/ffi/fuzzy.rs` implementing `fuzzy_match` with the `bitstr_t` and score contract; C `fuzzy.c` kept as `libtermo_c_fuzzy` for a differential fuzzer on match sets; `termo.api.fuzzy` and `window/switch.c` unchanged | `tests/fuzz/fuzzy-fuzzer.c` no divergence on match sets; `test_lua.c` and the palette spec unchanged; a 50k-line scrollback search through `termo.fuzzy` in a Lua spec completes inside the budget |
| 17 | **Workspace and `termo-mcp`.** `crates/Cargo.toml` workspace, `crates/termo-control` (control-mode codec: blocks, `%output` octal decoding, notifications) and `crates/termo-mcp` (stdio and streamable HTTP; tools `list_*`, `read_pane`, `send_keys`, `run_command`, `wait_for`, `split`, `new_window`, `capture_command_output`; resources for screens and notifications); `docs/mcp.md`; `justfile` `mcp`; nightly job building `crates/` with cargo (not in the gate) | `tests/e2e/test_mcp.py`: a Python MCP client over stdio lists tools, reads a pane, waits for `pane-command-finished` of a `sleep 1` with exit 0, captures the last output; `cargo test` in `crates/` green; the gate is untouched |
| 18 | **`termo record` / `termo stream`.** `crates/termo-record`: `pipe-pane` reader → asciicast v3 writer with `r` on `pane-resized` and `m` on `pane-shell-prompt`; `stream` over WebSocket; replay through `send-keys -l` | e2e: a recorded `.cast` of a fixture has the header, `o` events and one `m` per prompt; replay into a pane reproduces the screen (`capture-pane` equal) |
| 19 | **QUIC attach.** `crates/termo-quicd` and the `quic://` transport in the `termo` client (a C-side `-Q` that execs the Rust client binary, no `quinn` in `libtermo`); session id + passkey, replay ring, per-pane streams | e2e over loopback: attach, kill the client's socket underneath, reconnect, the screen resumes without a gap |
| 20 | **Web bridge.** `crates/termo-web` over `termo-control` with an xterm.js page | e2e with a WebSocket client asserting the first screen frame |
| 21 | **`input/` in Rust.** A plan step written after 9, with step 2's parser-stage numbers; the constraints are spec §5 and the worker shape spec §7 | — |
| 22 | **Resurrect** (spec §8.8). C: `resurrect` option in `options-table.c` (session scope, CHOICE `off|layout|commands|screen`, default `off`), `resurrect-commands` string option, `server-exit` event fired in `server_send_exit` before the `session_destroy` loop plus its hook entry; `etc/termo.conf` sets `resurrect layout`. Lua `session.lua`: `save`, `save_all`, `restore`, `restore_all`, `list`; event-driven debounced save plus `session-closed` and `server-exit`; one JSON per session in `~/.local/share/termo/sessions/`; `restore_all()` from `init.lua` with synchronous `cmd()`; `commands` via `ps -ao ppid,args` and `send-keys` after `pane-shell-prompt` (or immediately without integration), `screen` via `capture-pane -epJ -S -` and `cat file; exec <shell>`; `docs/man/termo.1` | Lua spec `session_spec.lua`: a two-window session with layouts and cwds round-trips, the file is written on `session-closed`; e2e `test_control.py`: `%server-exit` on `kill-server` and the file written before the server is gone; e2e `test_session.py`: a server started with a fixture `init.lua` restores a saved session before `attach` succeeds, `commands` mode re-sends an allowlisted command (fixture shell with OSC 133), `screen` mode shows the captured text in `capture-pane`, a layout string the parser rejects falls back to `tiled` |
| 23 | **Floating pane options** (spec §8.9). C: window options `float-width`, `float-height`, `float-position`, `float-border-style`, `float-border-lines` read by `layout_floating_args_parse` when the flag is absent; fix `#{?floating_pane_flag}` to `pane_floating_flag` in `key-bindings.c`; `etc/termo.conf` keys (`prefix f`, `prefix F`, `prefix C-f`); `float.lua` stops passing flags the options cover; `docs/man/termo.1` | unit `test_layout.c`: a float created without flags gets the option sizes and position; e2e `test_layout.py`: `set -w float-width 60` then `new-pane` reports 60 in `#{pane_width}`, the resize binding routes to the float resize (the typo fix), the `termo.conf` keys create and toggle a float on a `Pty` |
| 24 | **Stacked panes** (spec §8.10). `src/core/termo.h`: `LAYOUT_CELL_STACK`, `LAYOUT_CELL_COLLAPSED`, `SPAWN_STACK`; `src/layout/layout.c`: the helpers and the special cases in `layout_fix_offsets1`, `layout_fix_panes`, `layout_resize_adjust`, `layout_set_size_check`, `layout_resize_child_cells`, `layout_resize_pane`, `layout_split_pane`, `layout_destroy_cell`, `layout_spread_cell`; `src/layout/custom.c`: `(...)` in dump, parse and check, and the `<...>` float round-trip fix; `src/window/window.c`: `layout_stack_expand` from `window_set_active_pane`, `window_pane_is_visible`, `window_get_active_at`, `window_pane_get_pane_status`; `src/screen/redraw.c`: the collapsed row over pane-border-status; commands `split-window -S`, `new-pane -S`, `join-pane -S`, `select-pane -S`; formats `pane_stacked_flag`, `pane_collapsed_flag`, `pane_stack_index`, `pane_stack_size`, `window_stacks`; `etc/termo.conf` keys (`M-s`, `M-S`) and the pane menu entry; `docs/man/termo.1`; `docs/SYNCING.md` (layout string divergence) | unit `test_layout.c`: split marks one expanded and collapsed rows at `sy 1`; expand swaps and keeps the stack height; window resize changes the expanded only; resize on a collapsed pane resizes the stack; destroying the expanded promotes the neighbour; one child collapses to a leaf; a plain split of a stack child treats the stack as one tile; dump uses parens and parses back; check rejects bad heights; presets flatten; spread skips; neighbour skips collapsed; a float dump round-trips. e2e `test_layout.py`: geometry and `#{window_layout}` after `split-window -S`, `select-pane` expands a collapsed pane, only the expanded grows on resize, zoom and unzoom restore the collapsed rows, the collapsed row shows the title through `nest()`, a click on the collapsed row expands it on a `Pty`, a float over a stack is unaffected, the formats; regress green |
| 25 | **Shell integration** (spec §8.11). `runtime/shell/bash/termo.bash`, `runtime/shell/zsh/{.zshenv,termo-integration}`, `runtime/shell/fish/vendor_conf.d/termo-shell-integration.fish`; `meson.build` installs `runtime/` regardless of LuaJIT; `src/core/util.c` `termo_runtime_dir()` replacing the static in `src/lua/runtime.c`; `src/core/spawn.c` `spawn_shell_integration()` on the no-command path; `options-table.c` `shell-integration` (session, CHOICE `off|detect|bash|zsh|fish`, default `off`); `etc/termo.conf` sets `detect`; `docs/man/termo.1` | e2e `tests/e2e/test_shell_integration.py` with a tmp `HOME` and `ZDOTDIR`, each shell skipped unless `shutil.which` finds a usable version: a bash, zsh or fish pane sets `#{pane_last_prompt_time}`, `send-keys false Enter` ends with `#{pane_command_running}` at 0 and exit status 1 on the line in `capture-pane -M`; `ZDOTDIR` is restored inside the pane; `off` leaves the environment untouched; `TERMO_SHELL_INTEGRATION=0` disables the marks; a non-shell command is not injected; hand-sourced marks are not doubled. Unit `test_spawn.c` for the detection helper (basename, macOS `/bin/bash`) |
| 26 | **Layouts as data and swap layouts** (spec §8.12). `layout.lua`: `apply` accepts a window entry of the session format, `dump`, `save`, `load`, `swap` over `select-layout`; `docs/example_init.lua` binds `swap` | Lua spec `layout_spec.lua`: dump→apply round trip equals `#{window_layout}`; `swap` cycles two saved layouts and skips one with a different pane count; e2e `test_layout.py`: a Lua key binding swaps layouts on a `Pty` |

## Two tracks

The steps split by what they touch, so two people (or two instances) work in parallel
without editing the same files.

**Track A, Rust** (worktree `../termo-rust`, branch `rust/006`): 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9,
then 14 (needs `sys.rs` and the allowlist), 16 (needs 8's fuzzer pattern), 17 → 18 →
19 → 20 (crates), 21. Files: `meson.build`, `meson_options.txt`, `tests/meson.build`,
`ci/`, `.github/workflows/`, `src/rs/`, `subprojects/`, `src/utf8/`, `src/grid/`,
`tests/fuzz/`, `tools/bench/`, `crates/`, plus the accessor rewrite of step 7 and the
`input.c` changes of steps 2 and 6.

**Track B, product** (worktree `../termo-product`, branch `product/006`, merged into
`refactor/to-termo` after each step): 10 modes and user commands, 11 copy-mode text
objects and prompt jumps, 12a `termopack`, 12b lazy, 13 agent-aware core, 15 health,
Neovim, sessionizer, 22 resurrect, 23 floating pane options, 24 stacked panes, 25 shell
integration, 26 layouts as data. Files: `runtime/lua/termo/`, `runtime/shell/`,
`tests/lua/`, `tests/e2e/` (new modules and fixtures), `docs/{plugins,api}.md`,
`docs/man/termo.1`, `docs/example_init.lua`, `etc/termo.conf`, and these C sites only:
`server/client.c` (`client-key-table-changed`, 3 lines), `server/server.c`
(`server-exit`), `core/prompt.c` (the Lua completion hook), `core/spawn.c`
(`spawn_shell_integration`), `core/util.c` (`termo_runtime_dir`), `core/key-bindings.c`
(the float typo), `window/copy.c` (new `-X` commands), `cmd/pane/capture.c` (`-M`),
`input/input.c` (`OSC 9;4` states, one function), `config/options-table.c` (the new
hooks and options), `tty/features.c` (`notify`), `src/lua/{api,ui}.c` (`notify`,
`complete`), and for row 24 only `src/layout/{layout,custom}.c`, `src/window/window.c`,
`src/screen/redraw.c`, `src/cmd/window/split.c`, `src/cmd/pane/{join,select}.c`,
`src/format/format.c`. Track B needs nothing from Track A: it builds and tests with
`-Drust=disabled` or before step 3 exists.

Where the tracks meet, and the rule:

- `window/copy.c` and `cmd/pane/capture.c`: B's step 11 adds functions; A's step 7
  rewrites field accesses in the same files. B lands 11 first; A's step 7 is mechanical
  and rebases over it (the accessor table in `docs/SYNCING.md` is what makes it
  mechanical). If 7 is ready before 11, 7 waits.
- `input/input.c`: A changes `input_parse_pane`/`input_parse` (step 2) and adds mode 2027
  (step 6); B changes `input_osc_9` (step 13). Different functions; git merges it.
- `config/options-table.c`: both add entries. Append-only in both tracks; trivial merge.
- `docs/man/termo.1`, `docs/SYNCING.md`, `docs/api.md`, `CLAUDE.md`: both edit. Each PR
  regenerates `docs/api.md` on its own branch; the other sections are appended, not
  reordered.
- `runtime/lua/termo/hints.lua`: B replaces it in step 10; A never touches it.
- `src/layout/`, `src/screen/redraw.c`, `src/core/spawn.c`, `src/server/server.c`: B only
  (rows 22, 24, 25); A's step 14 adds one call in `spawn_pane` after B's row 25 has
  landed, so the two changes to `spawn.c` never cross.
- `src/format/format.c`: A's step 7 rewrites grid field accesses; B's row 24 appends
  format callbacks. Append-only on B's side; git merges it.
- `tests/e2e/termo.py` and `tests/e2e/conftest.py`: A's step 1 imports them for the
  benchmark and does not change them; B may add helpers, appended.

Worktree recipe for Track B: `git worktree add ../termo-product -b product/006
refactor/to-termo`, then `meson setup build -Db_sanitize=address,undefined
-Dbuildtype=debugoptimized` inside it (the `.claude/settings.json` hook rebuilds
`$CLAUDE_PROJECT_DIR/build`, which is the worktree's own). A worktree cut from `main` is
old tmux and is wrong.

## Order of PRs and what may interleave

Within Track A, 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 is the critical path; 14 and 16 slot
in after 4 and 8 respectively; 17 to 20 come after B's 13 (the events they consume) and
are ordered by value; 21 is opened when 9 closes. Within Track B the order is 10 → 12a →
25 → 22 → 12b → 23 → 26 → 15 → 11 → 24 → 13: 25 early because 11, 13 and 22 consume its
marks, 12b after 10 because `cmd` triggers need `termo.command`, 24 late because it is the
largest C piece and lands after A's steps 2 and 3 are in the tree.

`ci/Dockerfile.ubuntu` and `ci/Dockerfile.alpine` change in step 3 only, from a branch of
the repository (a fork cannot push the image). Every step that adds a `termo.api`
function regenerates `docs/api.md`; every step that adds a command or option touches
`docs/man/termo.1`; every step that diverges from upstream adds to `docs/SYNCING.md`.

## Files

Create: `tools/bench/bench.py`, `tools/bench/scenarios/*/{setup,benchmark}`,
`docs/bench.md`; `src/rs/**` (`lib.rs`, `sys.rs`, `bindings/wrapper.h`, `utf8/*.rs`,
`grid/*.rs`, `ffi/*.rs`, `Cargo.toml`, `cbindgen.toml`, `rustfmt.toml`);
`subprojects/{libc,memchr,unicode-width,unicode-segmentation,lz4_flex,landlock,seccompiler,nucleo-matcher}.wrap`
and their `meson.build`; `src/grid/grid.h`; `src/compat/sandbox-darwin.c`;
`tests/fuzz/{utf8,grid,fuzzy}-fuzzer.c` with corpora and dicts; `tests/lua/{mode,command,pack,health,session,notify}_spec.lua`;
`tests/e2e/test_mcp.py`, `tests/e2e/test_shell_integration.py`,
`tests/e2e/fixtures/{osc133.sh,plugin_lib.c,graphemes.sh,resurrect_init.lua}`;
`tests/lua/layout_spec.lua`; `tests/unit/test_spawn.c`;
`runtime/lua/termo/{mode,command,health,nvim,session,sessionizer,sandbox}.lua`;
`runtime/shell/bash/termo.bash`, `runtime/shell/zsh/{.zshenv,termo-integration}`,
`runtime/shell/fish/vendor_conf.d/termo-shell-integration.fish`;
`crates/**`; `docs/{crates,plugins,mcp}.md`.

Modify: `meson.build`, `meson_options.txt`, `tests/meson.build`, `justfile`,
`ci/Dockerfile.ubuntu`, `ci/Dockerfile.alpine`, `.github/workflows/{ci,nightly}.yml`,
`src/core/termo.h`, `src/core/util.c` (`getversion`), `src/window/window.c`
(`bufferevent_set_max_single_read`), `src/input/input.c` (parse budget, `since_ground`
cap, mode 2027, OSC 9;4 states), `src/grid/grid.c` (growth, accessors, then deleted),
`src/screen/{write,screen,redraw}.c`, `src/window/copy.c`, `src/cmd/pane/capture.c`,
`src/format/format.c`, `src/core/session.c`, `src/tty/{draw,features,keys,tty}.c`,
`src/window/{panes,window}.c`, `src/cmd/pane/resize.c` (accessors), `src/core/control.c`
(watermark), `src/server/client.c` (`client-key-table-changed`), `src/server/server.c`
(`server-exit`), `src/core/prompt.c` (Lua completion), `src/config/options-table.c`
(`unicode-width-cjk`, `agent-idle-time`, `sandbox-profile`, `resurrect`,
`resurrect-commands`, `shell-integration`, `float-*`, the hooks), `src/core/spawn.c`
(`spawn_shell_integration`, then `termo_sandbox_apply`), `src/core/util.c`
(`termo_runtime_dir`), `src/lua/runtime.c`, `src/core/key-bindings.c` (float typo, pane
menu), `src/layout/{layout,custom}.c` and `src/core/termo.h` (stacks), the `-S` flag in
`src/cmd/window/{new,split,respawn}.c`, `src/cmd/pane/{respawn,join,select}.c`,
`src/core/fuzzy.c` (`HAVE_RUST` dispatch), `src/lua/{api,ui}.c` (`notify`, `complete`),
`runtime/lua/termo/{init,pack,hints,palette,float,layout}.lua`, `etc/termo.conf`
(copy-mode, float and stack bindings, `resurrect`, `shell-integration`),
`docs/man/termo.1`, `docs/api.md`, `docs/SYNCING.md`, `docs/ci.md`,
`docs/example_init.lua`, `CLAUDE.md`, `README.md`.

Reuse untouched: `key_bindings_add/remove`, `switch-client -T` and `#{client_key_table}`
(modes), `command-alias` expansion in `cmd-parse.y` (user commands), `events_fire*` and
`event_payload_*` (every new event), `job_run`/`termo.api.system` (pack, sessionizer),
`status_prompt_set` (completion hook only), `pipe-pane` (recording), control mode as is
(every auxiliary executable), `fdforkpty` and the child sequence in `spawn_pane` (one
call added), `screen_write_*` (the grid module is only ever driven through it).

## Risks

- **bindgen and C23.** `constexpr` objects and `[[nodiscard]]` on a header bindgen has no
  changelog entry for. Step 4 verifies on the first run; the fallback is the `#define`
  mirror in `wrapper.h` (already the design) and, if libclang rejects `-std=c23`, a
  `wrapper.h` that includes `termo.h` under `-std=c2x` with the two C23-only constructs
  behind `#ifdef __clang_analyzer__`-style guards. No change to `termo.h` for bindgen's
  sake.
- **`rust.test` under `b_sanitize=address`** (Meson issue #11741: the test executable is
  linked by rustc without the ASan runtime). Fallback in step 5: build the `#[test]`
  target with `override_options: ['b_sanitize=none']`, or run the Rust unit tests through
  `cargo test` in the `lint` job only. The C unit suite is the ABI contract either way.
- **`-Cpanic=abort` and tests.** rustc's libtest needs unwinding to report a failing test
  as a failure rather than an abort. The test target compiles with `-Cpanic=unwind`
  (`rust_args` override on that target); the library the binary links stays `abort`.
- **Two ASan runtimes.** Nobody passes `-Zsanitizer` in the gate; the nightly fuzz job
  does and runs clang-20 only. A `meson.build` check errors if `rust_args` contains
  `-Zsanitizer` while `cc.get_id() == 'gcc'`.
- **rustup in the Alpine and FreeBSD jobs.** First nightly after step 3 verifies both;
  fallback is `-Drust=disabled` on those cells with the variant recorded in `docs/ci.md`
  until rustup works there.
- **Grid encapsulation (step 7) versus upstream.** `copy.c` and `capture.c` are the two
  files upstream changes most. After step 7 every cherry-pick into them is ported through
  the accessors; the step's PR includes a `docs/SYNCING.md` recipe (a `sed` table from
  field access to accessor) so the port is mechanical.
- **Page compression portability.** `madvise(MADV_DONTNEED)` on Linux and
  `MADV_FREE_REUSABLE` on macOS are the two verified paths; FreeBSD/NetBSD/OpenBSD get
  `MADV_FREE` if present, else the page is dropped and reallocated on inflate. The RSS
  gate is measured on Linux and macOS only.
- **Reflow parity.** `grid_reflow` has upstream quirks the differential fuzzer will find;
  the Rust module reproduces them (the oracle is the C output), and each one is listed in
  the PR as a candidate fix for a later item, never fixed silently.
- **Idle compression on the loop thread.** The timer callback inflates and deflates
  pages on the loop; a 50k-line pane compresses in slices of at most 2 ms per tick
  (measured in step 8) so a redraw is never delayed by it.
- **Landlock ABI on old kernels.** Ubuntu 24.04 (6.8) has V4; V5+ features are used only
  when present (`ABI::V4` minimum, `compat` best-effort mode of the crate) and `termo
  health` reports the level. macOS `sandbox_init` is deprecated but present in Tahoe and
  is what Codex ships; the shim isolates it.
- **Mode 2027 without outer support.** The documented limitation in spec §3.3; the e2e
  case asserts termo's own cells, not the outer terminal's rendering.
- **`hints.lua` users.** `termo.hints` stays as an alias of `termo.mode` for one release
  with a deprecation message in `termo health`.
- **Stacks versus upstream `layout.c`.** The flag keeps every `switch (lc->type)`
  byte-identical; the special cases sit in six functions upstream rarely touches, and
  the roadmap limits cherry-picks to `input/`, `tty/`, `grid/`, `utf8/` anyway. An old
  tmux reading a termo layout string with `(` fails with "invalid layout", never
  misreads it. A collapsed pane's pty keeps its last size, so `pane_height` reports the
  expanded height; `pane_collapsed_flag` is the guard and the man page says so.
- **Shell integration side effects.** bash `--posix` skips the native startup files and
  the script replays them in Kitty's and Ghostty's order; bash before 4.4 has no `PS0`
  and gets no `C` mark; zsh users with their own `ZDOTDIR` rely on `.zshenv` restoring it
  before `.zshrc` runs; fish before 3.3 lacks the events; `ssh` inside a pane does not
  carry the variables. The option is off in compiled defaults and
  `TERMO_SHELL_INTEGRATION=0` turns it off per pane.
- **Resurrect at start.** `restore_all()` runs inside config loading, so it must stay
  synchronous; anything that waits (a `commands` re-send) runs after the first attach and
  is visible as it happens. A save file from a newer `version` is ignored with a message.
- **Time.** The critical path (1 to 9) is the largest sequence of PRs in the project so
  far; the product steps are independent of it on purpose so a slow grid does not block
  the visible features.

## Close

The item closes when steps 1 to 17 and 22 to 26 are landed, `-Drust` is required, `docs/SYNCING.md`
carries the porting rules, `just bench --compare` against the step-1 baseline is within
10% on every scenario with `+rust`, and `ROADMAP.md` Phase 5 is marked done with the
date. Steps 18 to 21 are scheduled after the close as their own PRs against this plan.

## Amendment (2026-09-15)

Track B only. Closed after the conversation of 2026-09-09 to 15, recorded in the intent
(axes 4 and 9, product table) and the spec (§8.1, §8.3, §8.8 to §8.12):

- `termopack` keeps `setup` as the entry point, gains `dependencies`, `clean`, a confirmed
  install, a lockfile in the config dir, and a lazy layer over `load` as a second row
  (12a, 12b). TPM plugins are not supported.
- Session persistence is its own row (22) with two C pieces, the `resurrect` option and
  the `server-exit` event; everything else is Lua over commands and `ps`, as
  tmux-resurrect does it. `termo`, `new -s` and `a -t` keep tmux's meaning.
- Rows 23 to 26 add floating pane options, stacked panes, shell integration provisioning
  and layouts as data. Quick select stays without a row.
- Row 10 gains the `@hints` option, the `Any` binding and the session-scope locked mode;
  the five-mode list is the example config, not a shipped set.
- Track B order: 10 → 12a → 25 → 22 → 12b → 23 → 26 → 15 → 11 → 24 → 13.
