# Spec 006: Rust where the bytes are untrusted, measured before moved

- Status: Approved 2026-09-09; amended 2026-09-15 (facts, §8.1, §8.3, §8.6, §8.8 to §8.12, §11, Verification)
- Intent: [`intent/006-rust.md`](../intent/006-rust.md)
- Research: [`research/006-rust.md`](../research/006-rust.md)
- Plan: [`plan/006-rust.md`](../plan/006-rust.md)

## Facts the design rests on

Measured in the tree on 2026-09-09 unless a source is named.

- **Build.** `meson.build` is C only (`project('termo', 'c')`, `meson_version >= 1.4.0`);
  `libtermo` is `static_library('termo', sources)` linked by `termo`, `termo-test` and,
  rebuilt with `-fsanitize=fuzzer-no-link` as `termo_fuzz`, by the four fuzzers
  (`tests/meson.build`). Meson 1.12.0 in `ci/Dockerfile.ubuntu` and locally; Alpine's apk
  Meson is 1.8. No `rustc`, `cargo` or `bindgen` in any CI image. Locally: rustc 1.98.0 via
  rustup, no bindgen.
- **Meson and Rust** (Meson docs and `mesonbuild/compilers/rust.py`): `rust_abi: 'c'` on a
  `static_library` produces a `staticlib`; a separate Rust library linked into a C target
  is the pattern that works on every Meson since 1.3 (mixed C/Rust targets exist since 1.9
  but need rustc as the linker); `rust.bindgen()` with `output_inline_wrapper` (1.3+)
  emits C wrappers for `static inline` functions; `rust.cbindgen()` since 1.12;
  `rust.test()` registers `#[test]`s; `rust_std=2024` is a compiler option; `werror`
  maps to `-D warnings`, `warning_level=3` to `-W warnings`, `b_ndebug` to
  `-C debug-assertions`; **`b_sanitize` adds no `-Zsanitizer` to rustc** ("Sanitizers are
  not supported yet for Rust code"), only the link args, so a stable rustc compiles under
  `-Db_sanitize=address,undefined` and the Rust objects are uninstrumented; Meson detects
  `-nightly` in the version and has a `rust_nightly` option (1.10); `rust_panic`,
  `rust_overflow_checks` and `rust_codegen_units` are documented for 1.13, not yet
  released, so `-Cpanic=abort` goes through `rust_args` until then.
- **Sanitizers in Rust** (tracking issue #123615): `-Zsanitizer` is nightly; ASan and
  LSan stabilisation as `-Csanitize` is in progress with Tier 2 targets carrying an
  instrumented std. rustc links its ASan runtime statically; GCC's dynamic `libasan.so`
  and rustc's static runtime in one process fail at start. Miri is a nightly component;
  `cargo miri` is its supported entry point.
- **`utf8/`** is 1289 lines (`utf8.c` 977, `combined.c` 312), 30 exported functions, 33
  consumer files. `struct utf8_data` is 35 bytes (`data[32]`, `have`, `size`, `width`);
  `utf8_char` packs size (5 bits), width+1 (3 bits) and either the UTF-8 bytes (≤3) or an
  index into two global RB trees (`utf8_item`, never freed, capped at 0xffffff). Widths
  come from an RB cache seeded by 160 hard-coded emoji/regional-indicator entries plus the
  `codepoint-widths` option, then `utf8proc_wcwidth` or `wcwidth`. `combined.c` decides
  ZWJ, variation selector, Hangul filler, regional-indicator pairs, skin-tone modifiers and
  Hangul Jamo composition; `screen_write_combine` applies it and caps a cluster at
  `UTF8_SIZE` (32) bytes. Consumers most used: `utf8_set` (34), `utf8_copy` (22),
  `utf8_fromcstr` (16), `utf8_strlen` (15), `utf8_open`/`utf8_append` (12 each).
- **`grid/`** is 2516 lines, 70 exported functions across `grid.c`, `view.c`,
  `reader.c`. Cells are `grid_cell_entry` (5 bytes packed) inline or an index into a
  per-line `grid_extd_entry` (23 bytes packed) array grown by one `xreallocarray` per new
  extended cell; `grid_line` carries `celldata`, `extddata`, `cellused/cellsize`
  (`u_short`), `extdsize`, `time`, `osc133_data` (prompt/command/output columns and exit
  status, upstream 3.8) and `flags`; `grid` is a flat `linedata[hsize+sy]` grown by one
  `xreallocarray` per scrolled line. **Eleven files outside `src/grid/` read those structs
  by field** (`window/copy.c` 68 sites, `cmd/pane/capture.c` 35, `screen/write.c` 21,
  `format/format.c` 20, `screen/screen.c` 16, `input/input.c` 16, `core/session.c` 4,
  `tty/draw.c` 3, `window/panes.c` 2, `cmd/pane/resize.c` 2); `GRID_LINE_*` flags are
  tested directly in six of them.
- **Output path.** `window_pane_read_callback` disables `EV_READ` after every parse and
  the loop re-enables it from `server_client_check_pane_buffer`; libevent's single read is
  4096 bytes and termo never calls `bufferevent_set_max_single_read`. `input_parse` has
  no byte budget. `screen_write_collect_add` leaves the fast path for any cell with
  `width != 1` or `size != 1`. `tty_block_maybe` drops the whole output buffer above
  `1 + sx*sy*8` bytes and forces a redraw 100 ms later.
- **Key dispatch.** `server_client_key_callback` (270 lines) keeps one `key_table` per
  client, resets to root silently on prefix timeout, on a non-repeat binding and on an
  unbound key, and swallows a key not found after starting in a non-root table.
  `server_client_set_key_table` is ten lines and notifies nothing. A Lua key handler gets
  `{client, key, table, mouse}`.
- **OSC 133.** `input_osc_133` already stores prompt, command and output columns and the
  exit status per line, keeps `cmd_start_time`, `cmd_end_time`, `cmd_status`,
  `last_prompt_time` and `PANE_CMDRUNNING` on the pane, and fires `pane-shell-prompt`,
  `pane-command-started` and `pane-command-finished` with `command_status`,
  `command_start_time`, `command_end_time` and `command_duration` in the payload.
- **Spawn.** `spawn_pane` forks with `fdforkpty`; the child, between the fork and
  `execvp`/`execl`, optionally moves into a cgroup (`systemd_move_to_new_cgroup`), sets
  `PWD`, termios, clears signals and fds, pushes the environment.
- **Control mode.** Lines in, `%begin`/`%end`/`%error` blocks and `%output %<pane>`
  with octal escapes out, `%extended-output` with age under `pause-after`,
  `%subscription-changed`, `%pause`/`%continue`; peers are checked by uid/gid through
  `server_acl_check`. The e2e `Control` class already speaks it from Python.
- **Lua.** 24 API functions in seven tables; `keymap_set` binds functions as
  `run-lua -r <ref>`; `format_add` registers a lazy callback on every tree;
  `hooks_valid_event_name` accepts hook option names and `@custom`; `prompt` supports
  `incremental`, `single` and reports `"move"` for up/down; `pack.lua` reads `name` and
  `main` from `termo.json`, clones with `--depth 1`, updates with `pull --ff-only`, runs
  the chunk under `setfenv` over a read-only `_G`. `require("ffi")` succeeds in the server.
  `src/core/fuzzy.c` is upstream's fzf-style matcher (`fuzzy_match`), used by
  `termo.api.fuzzy` and `window/switch.c`.
- **Added 2026-09-15.** `default-client-command` (server option, default `new-session`) is
  what a bare `termo` runs. Config runs on the global queue with no client and the first
  client's command is blocked until `cfg_done` (`cfg.c`), so synchronous `cmd()` calls
  from `init.lua` create sessions before that command runs; Lua's async work (`system`,
  timers) runs after it. `server_send_exit` (`server.c`) flushes, marks clients for exit
  and destroys sessions one by one; nothing fires before the destroy loop, and
  `session-closed` arrives with the session already gone. `pane-command-started` and
  `pane-command-finished` carry status and times, not the command text;
  `#{pane_start_command}` is the pane's creation command (empty for a shell) and
  `#{pane_current_command}` the foreground process name without arguments.
  tmux-resurrect gets the argv with `ps -ao ppid,args` filtered by the pane pid, and
  restores screen content by creating the pane with `cat file; exec $SHELL`.
  `input_parse_buffer` is called only from the pty read path; `input_parse_screen` by
  popups and copy mode. Floating panes are complete in C (`layout_floating_*`,
  `new-pane`, `break-pane -W`, `join-pane`, `move-pane -P`, z-index, modal) with size and
  position from per-command flags only (`layout_floating_args_parse` defaults to
  `w->sx/2` by `w->sy/4`) and no option; `layout_dump` appends floats as `<...>` and
  `layout_parse` then rejects the string, so a float layout does not round-trip; no
  stacked layout exists; every `switch (lc->type)` (six in `layout.c`, five in
  `custom.c`) assumes the three types. `status` is a session CHOICE option (`off`, `on`,
  `2`..`5`), `status-format[]` a session array; the text of a row can be per client
  (`#{client_key_table}`) but the row count cannot. An overlay's `overlay_key` returning
  0 keeps it, 1 clears it, anything else clears it and processes the key, so a persistent
  overlay cannot pass keys through. `spawn_pane` runs `default-shell` as a login shell
  (`execl(shell, "-name")`) when the pane has no command and `$SHELL -c` otherwise;
  Kitty and Ghostty provision their prompt hooks by environment at spawn (`ENV` plus
  `--posix` for bash, `ZDOTDIR` for zsh, `XDG_DATA_DIRS` for fish) and both document that
  their hooks are not applied inside tmux.
- **Tests.** `tests/unit/test_utf8.c` has 17 cases over the decoder, packing, vis, widths
  and `combined.c`; `test_grid.c` 35 cases; `test_input.c` 38 including
  `osc133_marks_prompt_command_and_exit_status`; `tests/fuzz/input-fuzzer.c` feeds
  `input_parse_buffer` on an 80x25 pane; the e2e harness has `Server`, `Control` and
  `Pty`. No benchmark exists.
- **Outside.** vtebench measures "the speed at which a terminal reads from the PTY" with
  twelve benchmarks (`cursor_motion`, `dense_cells`, `light_cells`, `medium_cells`,
  `scrolling*` ×5, `sync_medium_cells`, `unicode`) and states it lacks frame rate and
  latency. Ghostty's parser-stage SIMD gave 7.3x ASCII, 16.6x UTF-8 decode, 2x on a real
  `cat`. Its 2026 scrollback compression: pages outside the viewport, LZ4, 250 ms idle
  debounce, 93.9% saving, zero throughput regression, `madvise` to return memory.
  `unicode-width` assigns width 2 to emoji ZWJ, modifier and presentation sequences and
  0 to `Default_Ignorable`/`Grapheme_Extend`; `unicode-segmentation` gives UAX #29
  grapheme boundaries with a `GraphemeCursor` for non-contiguous text. asciicast v3 is a
  JSON header plus `[interval, code, data]` lines with codes `o i m r x`. Landlock's Rust
  crate covers ABI V1–V7 (`Ruleset`, `PathBeneath`, `NetPort`). `nucleo-matcher` exposes
  `Matcher` and `Pattern::parse`. WezTerm's plugins are Lua-only git checkouts;
  Neovim's `vim.pack` has `src/name/version/data`, a lockfile, `PackChangedPre/PackChanged`
  and a confirmation buffer.

## 1. Toolchain and build

### 1.1 Floor and options

`meson.build` adds `add_languages('rust', native: false, required: get_option('rust'))`
behind a new feature option `rust` (`meson_options.txt`, default `auto`: enabled when
`rustc >= 1.98` is found, disabled otherwise; `-Drust=enabled` is what CI passes). With
Rust found: `default_options` gains `rust_std=2024`; a probe compiles a one-line crate
using `let` chains and `Vec::into_raw_parts` and fails with
`error('termo needs Rust 1.98+: rustup toolchain install stable')`; `-DHAVE_RUST=1` is
added to `c_args`, and the C tree keeps building without it (the C implementation of each
module stays until the Rust one is the only one, §3.5).

Warnings: `rust_args: ['-D', 'warnings']` is what `-Dwerror=true` already adds; the crate
roots carry the lint set (§1.4). Release and debug: Meson's `buildtype` maps to
`-C opt-level`; `-Cpanic=abort` and `-Coverflow-checks=on` are passed in `rust_args` for
every Rust target (overflow checks stay on in release: the leaf modules are the place
where an overflow is a bug, and the cost is a branch on arithmetic that is already
`ckd_*` in C). `b_lto` applies to rustc as `-Clto`.

### 1.2 Crate layout

One crate, flat, at `src/rs/`:

```
src/rs/
  Cargo.toml          # tooling only: clippy, miri, rust-analyzer, rustdoc; never the build
  lib.rs              # #![forbid(unsafe_code)] except the modules below that opt out
  bindings/           # bindgen output for termo.h, generated into the build dir; unsafe
  sys.rs              # the safe abstractions over bindings; the only unsafe termo writes
  utf8/               # module: decoder, widths, graphemes, packing
  grid/               # module: cells, lines, pages, history, marks
  ffi/                # #[unsafe(no_mangle)] extern "C" entry points, one file per module
  bench/              # (later) parse-stage microbenchmarks under #[cfg(test)]
```

`#![forbid(unsafe_code)]` is crate-wide with `#[allow(unsafe_code)]` on `bindings`,
`sys.rs` and `ffi/` only, so `utf8/` and `grid/` are safe Rust by the compiler's word. The
decision to split into a workspace (`crates/`) is taken only when an auxiliary executable
exists (§9); until then `Cargo.toml` has no `[[bin]]`.

Vendored crates live in `subprojects/` as Meson wraps with a `meson.build` each, built as
`static_library(..., rust_abi: 'rust')` and passed through `rust_dependency_map`. The
allowlist for this item: `libc`, `memchr`, `unicode-width`, `unicode-segmentation`,
`lz4_flex` (safe-encode/safe-decode features). Each entry records version, licence,
`cargo vet`-style review note and the commit that vendored it in `docs/crates.md`.
`Cargo.toml` references the same sources by `path` so `cargo clippy` and `cargo miri` see
exactly what Meson builds.

### 1.3 Meson targets

```meson
rust = import('rust')
termo_bindings = rust.bindgen(
  input: 'src/rs/bindings/wrapper.h',      # includes termo.h with the C23 flags
  output: 'bindings.rs',
  output_inline_wrapper: 'bindings_inline.c',
  include_directories: inc_dirs,
  c_args: c_args + ['-std=c23'],
  args: ['--allowlist-file', '.*/termo\.h', '--no-layout-tests',
         '--rustified-enum', 'utf8_state|hanguljamo_state', '--use-core'],
)
termo_rs = static_library('termo_rs',
  structured_sources(['src/rs/lib.rs', ...], {'bindings': termo_bindings}),
  rust_abi: 'c',
  rust_args: rs_args,
  rust_dependency_map: {'unicode_width': 'unicode-width', ...},
  link_with: vendored_libs,
)
termo_rs_header = rust.cbindgen('src/rs/lib.rs', 'termo_rs.h', config: 'src/rs/cbindgen.toml')
```

`termo_lib` gains `link_with: termo_rs` and `bindings_inline.c` in its sources;
`src/core/termo.h` includes `termo_rs.h` under `HAVE_RUST`. `termo_fuzz` links the same
`termo_rs` (the Rust objects carry no fuzzer instrumentation; §1.6). `rust.test('rs',
termo_rs, suite: 'unit', protocol: 'tap'?)` registers the `#[test]`s as the `unit-rs`
Meson test (Meson runs the libtest harness; TAP is not available, plain exit status is).

bindgen against a C23 header is verified on the first run (plan step 1): `nullptr` and
`bool` are fine for libclang 20; `constexpr` objects (`UTF8_SIZE`, `PANE_MINIMUM`,
`INPUT_BUF_DEFAULT_SIZE`) are expected to come out as statics without a definition, and
the spec's answer is a `#define` mirror in `wrapper.h` for the constants Rust reads (a
short list), not a change to `termo.h`.

### 1.4 Lint set and conventions

Crate root:

```rust
#![forbid(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn, improper_ctypes, improper_ctypes_definitions,
        clippy::undocumented_unsafe_blocks, clippy::arithmetic_side_effects,
        clippy::cast_possible_truncation, clippy::cast_sign_loss,
        clippy::indexing_slicing = "allow" /* bounds checks are the point */)]
#![warn(clippy::pedantic, missing_docs)]
```

Every `unsafe` block has a `// SAFETY:` line naming the C contract it relies on. Every
`extern "C"` function takes `(*const u8, usize)` or a `*const T` plus explicit length,
never a slice; the slice is built once at the edge. Every `#[repr(C)]` mirror carries
`const _: () = assert!(size_of::<T>() == N && align_of::<T>() == A)` and the C side gets a
`static_assert` on the same numbers. Panics abort (1.81 semantics, `-Cpanic=abort`);
nothing catches them. Allocation is the system allocator (default). No `static mut`;
module state that C keeps in file statics moves into a context struct owned by the C
caller or a `LazyLock`. `rustfmt` with the default style edition 2024; `cargo clippy
--all-targets -- -D warnings` and `cargo fmt --check` run in the `clang-tidy` gate job
(renamed `lint`), which is the only job that needs the tooling `Cargo.toml`.

### 1.5 CI

`ci/Dockerfile.ubuntu` installs rustup pinned to `1.98.0` (`RUSTUP_TOOLCHAIN`), the
`clippy`, `rustfmt` and `rust-src` components, `bindgen-cli 0.73.2` and `cbindgen` via
`cargo install --locked`, plus a nightly toolchain with `miri` and `rust-src` used only by
`nightly.yml`; `ci/Dockerfile.alpine` installs rustup the same way (Alpine's rust is
1.87); the FreeBSD job installs rustup in `prepare` (ports carry 1.94). The image tag
changes, so the Dockerfile change lands from a repository branch (`docs/ci.md`). The gate
jobs pass `-Drust=enabled`. `nightly.yml` gains `rust-verify`: `cargo +nightly miri test`
in `src/rs/` (pure-Rust tests, no FFI), and the `fuzz` job builds the Rust objects with
`RUSTFLAGS=-Zsanitizer=address -Cpasses=sancov-module -Cllvm-args=-sanitizer-coverage-level=4
-Cllvm-args=-sanitizer-coverage-inline-8bit-counters` under clang-20 (clang links ASan
statically, so the runtime clash is with gcc only, which the fuzz job does not use).
Releases and the merge gate never see nightly.

### 1.6 Evaluation harness

- **Differential fuzzing.** Each Rust module ships behind the same C ABI as the C module it
  replaces, and while both exist the build carries both (`-Drust=enabled` selects Rust at
  link time by symbol; the C objects are compiled into `libtermo_c_<module>` for the
  fuzzers only). The fuzzer for a module (`tests/fuzz/utf8-fuzzer.c`, `grid-fuzzer.c`,
  and `input-fuzzer.c` for the grid through `screen_write`) calls both through
  prefixed symbols (`termo_c_utf8_open` vs `utf8_open`, produced by
  `objcopy --prefix-symbols` on the C archive, or by a `-DTERMO_C_PREFIX` build of the
  module) and aborts on the first difference in outputs, widths or `grid_string_cells`
  text. Corpora: the existing `tests/fuzz/corpus/input` plus `utf8` (UTF-8 test file,
  emoji ZWJ sequences, Hangul Jamo, regional indicators, overlongs, surrogates) and
  `grid` (screen-write action streams recorded from the e2e runs).
- **Unit.** `tests/unit/test_utf8.c` and `test_grid.c` run unchanged against the Rust
  module (they are the ABI contract); `src/rs/**/*.rs` `#[test]`s cover the internals
  (packing, page lifecycle, compression round trip, grapheme boundaries), run by Meson and
  by Miri nightly.
- **e2e and regress.** `meson test` and `just upstream-regress` green with `-Drust=enabled`
  and `-Drust=disabled`.
- **Benchmark.** §2.

## 2. Benchmark (step 0)

`tools/bench/`: `bench.py` (the e2e `Server` and `Pty` classes imported from
`tests/e2e/termo.py`), `scenarios/` (one executable per scenario, vtebench's shape: an
optional `setup` and a `benchmark` whose stdout is the payload), `just bench [scenario]`.

- **Scenarios**: `ascii` (dense printable text), `utf8` (CJK, emoji ZWJ, combining marks,
  the `unicode` benchmark), `sgr` (dense attribute and RGB changes), `scroll` (full-screen
  and region), `resize` (reflow of a 50k-line history through ten widths), `cursor`
  (vtebench's `cursor_motion`), `sync` (DEC 2026 batches). Payloads are generated once
  into `build/bench/payloads/` (vtebench's generators, vendored as scripts) and are the
  same bytes on every run.
- **Two measurements per scenario**, because the intent asks where the time splits:
  1. *apply stage* (parser + grid + redraw to one client): a `Pty` client of 80x24 (and
     200x60) attached to a pane running `cat payload`; the harness reads the pty output
     to `/dev/null` and reports bytes/s from `cat` start to the last byte drawn (the pane's
     `#{pane_output_generation}` stops changing and the pty is idle).
  2. *parse-only stage*: the same `cat` in a detached session with no client; the harness
     polls the output generation. The difference between 1 and 2 is the redraw cost; the
     parser-stage number the research quotes from Ghostty is measurement 2.
- **Output**: `build/bench/<git-sha>.json` with per-scenario bytes/s, wall time, peak RSS
  of the server (`/proc/<pid>/status` or `ps`), and the machine; `just bench --compare
  <sha>` prints the ratio and exits non-zero when any scenario regresses by more than
  10%. That comparison is the roadmap gate, run by hand before a module lands and after;
  it does not run in CI (runner noise).
- **C fixes measured with it before any Rust** (each its own commit with before/after
  numbers in the commit message): `bufferevent_set_max_single_read(wp->event, 65536)` at
  pane creation with `input_parse` given a per-call budget so a burst cannot starve the
  loop (a loop iteration parses at most N bytes per pane, then yields; N chosen by the
  benchmark), and `grid_scroll_history` growing `linedata` geometrically instead of by
  one. The `tty_block_maybe` drop policy is left as is in this item.

## 3. `utf8/` in Rust (first module)

### 3.1 ABI kept

All 30 functions in `termo.h` lines `utf8.c` and `utf8-combined.c` keep name, signature
and semantics, implemented in `src/rs/ffi/utf8.rs` over `src/rs/utf8/`:
`utf8_towc`, `utf8_fromwc`, `utf8_update_width_cache`, `utf8_build_one`, `utf8_from_data`,
`utf8_to_data`, `utf8_set`, `utf8_copy`, `utf8_open`, `utf8_append`, `utf8_isvalid`,
`utf8_strvis`, `utf8_stravis`, `utf8_stravisx`, `utf8_sanitize`, `utf8_strlen`,
`utf8_strwidth`, `utf8_fromcstr`, `utf8_tocstr`, `utf8_cstrwidth`, `utf8_padcstr`,
`utf8_rpadcstr`, `utf8_cstrhas`, `utf8_has_zwj`, `utf8_is_zwj`, `utf8_is_vs`,
`utf8_is_hangul_filler`, `utf8_should_combine`, `hanguljamo_check_state`.
`struct utf8_data` stays 35 bytes with the same field order (`#[repr(C)]`, asserted);
`utf8_char` keeps its bit layout (the grid stores it). Returned `char *` buffers are
allocated with `libc::malloc` so the C callers' `free` stays correct; `xmalloc`'s abort
on OOM is matched by `handle_alloc_error` → abort.

Two behaviours change and are documented as such: `utf8_append` returns `UTF8_ERROR`
instead of `fatalx` on the two internal-overflow checks (unreachable, but an error is the
honest failure mode), and `utf8_from_data` returns `UTF8_ERROR` for `width > 2` instead of
aborting the server.

### 3.2 Widths

Width resolution order: the `codepoint-widths` option cache (kept, same option, same
semantics, now a `HashMap<char, u8>` rebuilt by `utf8_update_width_cache`), then the
160-entry default table (kept verbatim: it encodes tmux's decisions on emoji that
`wcwidth` gets wrong), then `unicode-width` at Unicode 17 with `width()` (non-CJK) and
`width_cjk()` behind a new server option `unicode-width-cjk` (default off), then 1. The
`utf8proc` dependency stays for `utf8proc_mbtowc`/`wctomb` only while the C module
exists; the Rust module decodes and encodes itself (`char::from_u32`, `encode_utf8`) and
`-Dutf8proc` becomes irrelevant to it (option kept for the C fallback build, removed with
the C module in §3.5). C1 controls are width 0, as today.

### 3.3 Graphemes and mode 2027

The cluster logic in `combined.c` and `screen_write_combine` is tmux's hand-written subset
of UAX #29 (ZWJ, VS16, skin tones, regional indicators, Hangul Jamo). The Rust module
keeps those predicates byte-for-byte as the ABI (§3.1) and adds the general rule behind
them: `utf8_should_combine` consults `unicode-segmentation`'s `GraphemeCursor` over the
previous cell's bytes plus the new codepoint, so any sequence UAX #29 calls one cluster
combines (flag sequences, keycaps, ZWJ chains longer than two, Indic conjuncts), capped by
`UTF8_SIZE`. The cluster's width is `unicode-width`'s width of the whole cluster (2 for
emoji sequences), which is what `force_wide` approximates today.

Mode 2027 is negotiated at both ends:

- *Pane side*: `DECSET 2027` / `DECRST 2027` set and clear `MODE_GRAPHEMES` on the
  screen (`input.c`, a new private mode); `DECRQM 2027` reports it. With the mode off the
  behaviour is exactly today's (tmux's subset); with it on, the general rule applies. The
  default is off, as in Contour's proposal, so nothing changes for a program that does not
  ask.
- *Outer terminal side*: `tty/features.c` gains a `graphemes` feature detected by
  `CSI ? 2027 $ p` at attach (reply `2027;1` or `;2` means supported), and `tty.c` sets
  `DECSET 2027` on such terminals when any pane has the mode on. On a terminal without it,
  termo draws clusters as today (the cursor is where `wcwidth` puts it) and the mismatch is
  confined to that client, which is the state of the art (Ghostty's article).

A pane that turns 2027 on inside a client without it gets width-2 clusters that the outer
terminal renders as wider; that is the documented limitation, not a bug termo can fix.

### 3.4 Interning

The two global RB trees for ≥4-byte sequences (`utf8_item`, unbounded, never freed)
become one `Vec<[u8; 32]>` plus a `HashMap<&[u8], u32>` behind a `Mutex` (the module is
called from one thread today; the lock is the assertion that keeps it correct if §7 adds a
parser worker), with the same 24-bit index space and the same "spaces on a missing index"
fallback in `utf8_to_data`. Memory is bounded by the index space, as today.

### 3.5 Removal of the C module

The C `utf8/` compiles only for `-Drust=disabled` and the fuzzers' differential half.
After one release with both (the plan names it), `src/utf8/*.c` is deleted, `-Drust`
loses `auto` for `utf8` and `docs/SYNCING.md` records that upstream changes to `utf8.c`
and `utf8-combined.c` are ported by hand (roadmap rule 6). Until then, every upstream
cherry-pick into `utf8/` is applied to both implementations.

## 4. `grid/` in Rust (second module)

### 4.1 The encapsulation step comes first, in C

A paged, compressible history cannot exist while eleven files index `gd->linedata[py]`,
read `gl->cellsize` or test `gl->flags` directly. Step 1 of the grid work is a C refactor
with no behaviour change: every access outside `src/grid/` goes through functions that
already exist (`grid_get_line`, `grid_peek_line`, `grid_get_cell`, `grid_line_length`)
or new ones added to `grid.c` (`grid_line_flags(gl)`, `grid_line_set_flags`,
`grid_line_osc133(gl)`, `grid_line_cellsize`, `grid_line_time`, `grid_hsize(gd)`,
`grid_scroll_added`). `struct grid_line` and `struct grid` become opaque outside
`src/grid/` (forward declarations in `termo.h`, definitions in `src/grid/grid.h`). The
unit suite and upstream regress prove the refactor; it is the largest divergence from
upstream in this item and is recorded in `docs/SYNCING.md` as such, because every future
upstream change that touches `gl->` in `copy.c` or `capture.c` is ported through the
accessors.

### 4.2 Representation

Behind the accessors, the Rust grid stores lines in **pages**: fixed-size arenas (64 KiB,
from a pool) holding cell entries and extended entries for a run of lines, plus a line
index (`Vec<LineRef { page, offset, cellused, cellsize, flags, time, osc133 }>`) that is
the only per-line allocation. Extended entries live in the page next to their line, so a
new extended cell is a bump allocation, not a `realloc`. The visible `sy` lines and the
most recent pages stay resident; pages older than the viewport are **compressed in idle
time** (LZ4 via `lz4_flex`, a 250 ms debounce timer armed from `grid_scroll_history` and
cancelled by any access, run from the libevent loop through a C-side `evtimer` the module
registers via a callback pointer, since Rust does not own the loop) and decompressed
transparently on access (`grid_get_line` on a compressed page inflates it and marks it
resident; ~26 µs per page in Ghostty's measurement). Physical memory of a compressed page
is returned with `madvise(MADV_DONTNEED)` on Linux and `MADV_FREE_REUSABLE` on macOS
(`libc`), keeping the virtual mapping so re-inflation is infallible. `hlimit` trimming
drops whole pages.

Kept semantics: `grid_cell_entry`'s 5-byte and `grid_extd_entry`'s 23-byte layouts (they
are the on-page format and `grid_string_cells` output is byte-identical); the 65535-cell
line cap; `grid_reflow`'s results (line-for-line equal to C, checked by differential
fuzzing on the `resize` scenario); `grid_duplicate_lines` and `grid_compare` for the
status and copy-mode code.

### 4.3 Marks and placements

`osc133_data` moves into the line index unchanged and gains a stable **mark id** per
prompt line so copy mode and the events of §8.4 can refer to a prompt after it scrolled.
The grid exposes `grid_marks_next(gd, from, kind)` / `grid_marks_prev` for the copy-mode
jumps (§8.2) and `grid_mark_range(gd, id, &start, &end)` for "last command output".
Image placements (Kitty/iTerm2, intent product table, "open") get a slot in the same
index (`placement id, x, y, w, h`) with no consumer in this item.

### 4.4 Reader and view

`grid/reader.c` (cursor motion) and `grid/view.c` (history-offset wrappers) stay C: they
are thin, upstream-churned (copy mode motions), and only call the accessor ABI. They are
candidates for later, not part of this item.

## 5. `input/` in Rust (third module): constraints only

The parser moves after the grid is stable. This spec fixes what it must keep: the state
table (`input_state_*`) and every handler's observable behaviour, the `input-buffer-size`
cap, `since_ground` capped at the same limit (a C fix that lands before, §6), the reply
path (`input_reply` through the pane's bufferevent), and the OSC 133 bookkeeping of the
facts above. Its design (typed actions, `memchr` ground-state scan, the worker of §7) is
the plan's step, written when the benchmark of §2 says what the parser stage costs.

## 6. Security fixes in C, in this item

Found while reading, independent of Rust, each with a unit case:

- `since_ground` capped at `input_buffer_size` (CSI parameter streams without a final byte
  grow it without bound; the 5 s ground timer covers only DCS/OSC/APC).
- `control_read_callback`'s input evbuffer gets a high watermark (64 KiB) so a line without
  `\n` from a same-uid peer cannot grow the server.
- `get-clipboard` keeps upstream's default; `etc/termo.conf` documents `set-clipboard on`
  next to the line, and `termo health` (§8.6) shows both values.

## 7. Concurrency shape (design, gated by §2)

When the benchmark shows the parse-only stage bounds a scenario, the parser runs on a
worker: one `std::thread` per pane (spawned on `input_init`, joined on `input_free`),
reading from a `crossbeam`-free `std::sync::mpsc` channel of byte chunks the loop thread
sends from `window_pane_read_callback`, producing `Vec<Action>` batches (`Print(Box<str>)`,
`Csi{..}`, `Osc{..}`, `Dcs{..}`, `Reply(Vec<u8>)`) sent back over a second channel; the
loop is woken by one byte on a `std::io::pipe` whose read end is a libevent `event`, and
applies the batch through `screen_write_*` in order. Nothing in the object graph is
touched off the loop thread; the parser owns only its state machine and the UTF-8 decoder
(§3.4's `Mutex` is what makes that legal). The `since_ground` buffer and `input-buffer-size`
apply per worker. This is the WezTerm/Alacritty shape with the apply step where termo's
fan-out to N clients already is.

## 8. Product axes: technical design

### 8.1 Modes and user commands (intent axis 9)

- **C**: `server_client_set_key_table` fires `events_fire_client("client-key-table-changed",
  c)` with the new table name in the payload; the name is added to the hook table so
  `hooks_valid_event_name` accepts it and `set-hook` can use it. Three lines plus the
  option entry.
- **Lua** (`runtime/lua/termo/mode.lua`, replaces `hints.lua`): `termo.mode.define{ name,
  key = leader-table key, sticky = true, keys = { k = { rhs, label } }, on_enter, on_leave }`
  binds every key in table `mode-<name>` (sticky by appending `switch-client -T`, the
  `hints.lua` mechanism, plus `Any` bound to stay), `Escape` and `i` leave, the hint bar
  segment renders from `#{client_key_table}`; `termo.mode.enter/leave/current(client)`;
  per-client state (count, pending operator) keyed by `ev.client`, reset on
  `client-key-table-changed`. A `modes` boolean user option (`@modes`, read by `mode.lua`)
  gates definition; `etc/termo.conf` leaves it off and `docs/example_init.lua` turns it on.
  Amended 2026-09-15: every mode table binds `Any` to `switch-client -T` itself, so an
  unbound key neither reaches the pane nor fires a root binding; the prefix key leaves a
  mode (the dispatch forces the prefix table and resets to root afterwards) and
  `mode.lua` treats that reset like `Escape`; locked mode is a session state, `set prefix
  None` plus a root binding that restores it, so it needs no C; a `hints` user option
  (`@hints`, `always` or `mode`, read by `mode.lua`) selects whether the second status row
  stays while modes are on (showing the current mode's keys, or the prefix table's) or
  appears only inside a mode. The example config's mode list (pane, window, session,
  resize, locked) is an example, not a shipped set: the runtime ships insert, normal and
  locked, and `termo.mode.define` builds the rest.
- **User commands** (`runtime/lua/termo/command.lua`): `termo.command.define(name, fn(args),
  { nargs = "*", complete = fn(prefix) -> list, desc })`; registered as a
  `command-alias[N]` of `run-lua -r <ref>` with the argument string appended, so
  `:name args` works in `command-prompt`, from `bind-key`, control mode and the CLI; the
  prompt's completion callback (`status_prompt_complete` in `prompt.c`) is extended in C to
  call a Lua completer for names it does not know (one hook, `PROMPT_COMPLETE_LUA`); the
  palette lists defined commands with `desc`. Plugin manifests declare them (§8.3).
- **Tests**: Lua specs for `mode`/`command`; e2e over a pty: enter a mode, the hint bar
  shows, an unbound key does not reach the pane, `:` runs a defined command with an
  argument; the `client-key-table-changed` payload in `test_control.py`.

### 8.2 Copy mode: text objects and prompt jumps (product table rows 2, 4, 8)

`src/window/copy.c` gains `-X` commands `select-word-inner`, `select-word-outer`,
`select-quote-inner`/`outer` (`"`, `'`, `` ` ``), `select-bracket-inner`/`outer`
(`()[]{}`), `select-paragraph`, `next-prompt`, `previous-prompt`, `select-command-output`
(the range between `GRID_LINE_START_OUTPUT` and `GRID_LINE_END_OUTPUT` of the enclosing
prompt, through §4.3's accessors), and `goto-mark <id>`. Bindings live in
`etc/termo.conf` (`copy-mode-vi`: `iw aw i" a" i( a( ip [[ ]] ` and `gO` for last
output). `capture-pane` gains `-M <mark>` to capture one command's output. e2e:
`test_screen.py` cases over a pane with OSC 133 emitted by a fixture shell script.

### 8.3 `termopack` after `vim.pack`, then lazy (intent axis 4)

Phase 1, `runtime/lua/termo/pack.lua`, `vim.pack` parity:

- `termo.pack.setup(specs, opts)`. A spec is `"owner/repo"` or `{ src, name, version,
  data, dependencies }`; `version` is a branch, tag, commit or `{ range = "1.x" }` resolved
  against `git tag` with a semver compare in Lua; `opts.load` is `true` (default), `false`
  (install and register only) or `function({ spec, path, manifest })`. `setup` is the one
  declaration in `init.lua`; the name follows the runtime's `*.setup` convention.
- Clone with `git clone --filter=blob:none` (history and tags present, blobs on demand);
  `--depth 1` cannot resolve tags or list `old..new`.
- Lockfile `termo-pack-lock.json` in `$XDG_CONFIG_HOME/termo/`, next to `init.lua`:
  `{ name = { src, rev, version } }`, read before install, written after every change; with
  a lockfile present `setup` installs the locked revs.
- Install asks first: `termo.ui.confirm` on the client that starts the server; with no
  client the plugins stay pending and are asked once, together, on the first
  `client-attached`. `update(names, { force, offline })` fetches, resolves the target rev
  per `version`, shows `git log --oneline old..new` per plugin in a `termo.ui.menu` and
  applies on confirm. `del(names)` removes; `clean()` removes every installed plugin not
  in the current list; a plugin dropped from the list is otherwise left on disk, inactive,
  and `termo health` lists it. `get(names)` returns `{ spec, path, rev, active, load_ms }`.
- `dependencies` (names or specs) in `termo.json` and in the user spec are installed and
  loaded before the dependant; a cycle is an error.
- Events `@pack-changed-pre` and `@pack-changed` with `{ name, kind = install|update|delete,
  spec, path, rev }` through `termo.emit`.
- Manifest fields read: `name`, `main`, `version`, `dependencies`, `lib` (a `cdylib` loaded
  with `ffi.load`, §8.7), and the phase-2 declarations `commands`, `keys`, `formats`,
  `events`, accepted by the parser from day one. `load_ms` is measured with `os.clock`
  around `main` and is the number that says whether lazy pays for startup or only for
  conditions. The chunk still runs under `setfenv` over a read-only `_G`.
  `install_dir` stays `~/.local/share/termo/pack/<name>`.

Phase 2, lazy as a layer over `load`:

- The manager knows nothing about triggers. A spec with triggers is loaded with
  `load = false`; stubs are registered; `termo.pack.load(name)` runs `main` on the first
  trigger and is idempotent.
- Triggers, in the user spec, with lazy.nvim's names where the analogue exists: `event`
  (`termo.on`, one shot), `keys` (a stub bound with `keymap_set`), `cmd` (a stub in
  `termo.command`, §8.1), `cond` (boolean or function; false keeps the plugin installed
  and inactive), `lazy = true` (manual only); termo's own `mode` (load when a mode is
  entered, from `client-key-table-changed`) and `format` (first evaluation of `#{name}`,
  through `format_add`).
- No key re-feed: the manifest declares `keys = { { table, key, command } }` and
  `commands = { { name, desc, nargs } }` with their action, so a stub is "load, then run
  the declared action". `send-keys -K` is not used: after a stub runs the client is back
  in `root`, so a re-fed prefix-table key would not land in the same table.
- A plugin is lazy iff it has triggers, in the manifest or in the spec; the spec wins.

### 8.4 Agent-aware core (intent axis 10)

In-process, C and Lua: a `waiting-for-input` state per pane derived from OSC 133
(`PANE_CMDRUNNING` and no output for `agent-idle-time`, a new option, default 3 s) or
from a program's explicit `OSC 9;4;<state>` progress sequence; `#{pane_command_state}`
format (`idle|running|waiting|finished`), `pane-waiting-for-input` on the event bus, and
`termo.notify(text)` writing OSC 9 or OSC 777 to the attached clients (`tty_putcode` on a
new `Notify` terminfo-less capability, feature-detected like `clipboard`). Out of process
(§9): `termo-mcp`.

### 8.5 Sandboxed panes (intent axis 10)

`new-window`/`split-window`/`new-pane`/`respawn-pane` gain `-S <profile>`; a profile is a
`sandbox-profile[]` array option (`name=fs:ro:/usr,/lib;fs:rw:$cwd;net:off;env:scrub`)
or a Lua table registered with `termo.sandbox.profile(name, {...})` that writes the
option. `spawn_pane` calls `termo_sandbox_apply(profile_name, cwd, cause)` in the child
between the cgroup move and `environ_push`; the function lives in `src/rs/ffi/sandbox.rs`
and is a no-op returning 0 on platforms without support. Linux: Landlock ABI ≥ V4
(`Ruleset` with `PathBeneath` rules, `NetPort` deny-all when `net:off`) plus a
`seccompiler` filter that blocks `ptrace`, `mount`, `bpf`, `kexec_*`, `init_module`;
macOS: `sandbox_init` with a generated Seatbelt profile (the same primitive Codex uses),
through a small C shim since the API is C-only; BSDs: `unveil`/`pledge` on OpenBSD where
they exist, no-op elsewhere with a `cause` that `termo health` reports. `env:scrub` drops
variables matching `*_TOKEN|*_KEY|*_SECRET|AWS_*` from the child's environment.
`#{pane_sandbox}` names the profile. e2e: a sandboxed pane cannot write outside `cwd`
(Linux only, skipped elsewhere).

### 8.6 Health, Neovim integration, sessions, sessionizer (Lua)

- `termo health` (`runtime/lua/termo/health.lua`, a user command and a palette entry):
  outer terminal features per client (`tty/features.c` names through
  `#{client_termfeatures}`), `set-clipboard`/`get-clipboard`, Lua runtime and budget,
  loaded plugins with rev and lockfile state, sandbox support on this OS, Rust module set
  (`#{version}` gains `+rust` when built with it).
- Neovim: `runtime/lua/termo/nvim.lua` provides `termo.nvim.navigate(dir)` (if the active
  pane runs nvim, `send-keys` the navigation key the plugin listens to, else
  `select-pane -<dir>`), detection through `#{pane_current_command}`; a separate
  `termo.nvim` plugin (its own repository, Lua) uses `termo run-lua -j` and `--server` to
  open paths. Scrollback in nvim: `termo.scrollback.edit()` captures with
  `capture-pane -S - -e` to a file and opens it in a float with `nvim +$line`.
- Sessions: §8.8 (moved 2026-09-15). `termo health` also lists inactive plugins, the
  `resurrect` mode and whether shell integration is active in the current pane.
- Sessionizer: `termo.sessionizer.open({ dirs = {...}, depth = 2 })` lists directories
  with `termo.system("fd" or "find")`, ranks with `termo.fuzzy` (nucleo later), creates
  or switches.

### 8.7 Native plugins through `ffi`

Documented policy (intent decision 4): `runtime/lua/termo/pack.lua` loads `lib` with
`ffi.load` when declared; the manifest must name the symbols in `cdef`; a panic in native
code aborts the server; the sandbox environment is not a security boundary. Nothing in the
core changes; `docs/plugins.md` (new) describes the Neovim-style trust model and the
`extern "C"` template for a Rust plugin.

### 8.8 Resurrect (added 2026-09-15)

- **C, two pieces.** Option `resurrect` in `options-table.c`: session scope, CHOICE
  `off|layout|commands|screen`, default `off` (rule 3), `etc/termo.conf` sets `layout`;
  `set -t work resurrect off` works like any session option. Event `server-exit` fired in
  `server_send_exit` after `cmd_wait_for_flush` and before the `session_destroy` loop,
  with a hook entry so `set-hook` and `termo.on` accept it; sinks run synchronously and
  write with `io.open`. Nothing else in C: sessions are created with `new-session -d`,
  `new-window`, `split-window` and `select-layout`; the foreground command comes from
  `ps`, the screen from `capture-pane` and `cat`, as tmux-resurrect does. A parser-feed
  command over `input_parse_buffer` and a per-platform argv in `osdep-*.c` were
  considered and left out until a need for cursor, alternate-screen or mark fidelity
  appears.
- **Lua**, `runtime/lua/termo/session.lua`: `save(session)`, `save_all()`,
  `restore(name)`, `restore_all()`, `list()`. Saving is event-driven with a debounce
  (`window-*`, `pane-*`, `session-*`, `window-layout-changed`) plus `session-closed` and
  `server-exit`; there is no interval. One JSON per session in
  `~/.local/share/termo/sessions/<name>.json`. `restore_all()` runs from `init.lua` with
  synchronous `cmd()`, so the sessions exist before the first client's command; `termo`
  bare, `new -s` and `a -t` keep tmux's meaning.
- **Format**, shared with §8.12:
  `{ version = 1, name, cwd, env, options, windows = { { name, layout, active, zoomed,
  panes = { { cwd, cmd, argv, screen } } } } }` where `layout` is the core's
  `layout_dump` string; restore creates N panes then applies it (`layout_parse` closes
  surplus cells, so the pane count must match). A parse error falls back to
  `select-layout tiled`.
- **Modes.** `layout`: sessions, windows, layouts, cwd, shell. `commands`: at save, one
  `termo.system({ "ps", "-ao", "ppid,args" })` per save filtered by `#{pane_pid}` (POSIX,
  every target); at restore, the command is sent with `send-keys` after
  `pane-shell-prompt` when the shell emits OSC 133 (§8.11), else immediately (the pty
  buffers it); filtered by option `resurrect-commands` (a string list, default
  `vi vim view nvim emacs man less more tail top htop`). `screen`: `capture-pane -epJ -S -`
  per pane into a file, and the pane is created with `cat file; exec <shell>` where
  `<shell>` is `default-command` or `default-shell`.

### 8.9 Floating pane options (added 2026-09-15)

Window options in `options-table.c`: `float-width` and `float-height` (a number of cells
or a percentage, the syntax of `display-popup -w`), `float-position` (CHOICE
`centre|cursor|cascade`), `float-border-style` (STYLE), `float-border-lines` (CHOICE, the
`popup-border-lines` list). `layout_floating_args_parse` reads them when the corresponding
flag is absent; `new-pane -x/-y/-X/-Y/-B` keep precedence. The resize binding's
`#{?floating_pane_flag}` in `key-bindings.c` is a wrong format name (always false) and is
corrected to `pane_floating_flag`. Default keys go in `etc/termo.conf` (`prefix f` new
float, `prefix F` toggle tiled/floating, `prefix C-f` front); `float.lua` keeps `setup{}`
for the keys and `new(spec)` stops passing flags the options cover.

### 8.10 Stacked panes (added 2026-09-15)

A stack is a `LAYOUT_TOPBOTTOM` node with `LAYOUT_CELL_STACK` (`0x2`) whose children are
leaves, exactly one of them without `LAYOUT_CELL_COLLAPSED` (`0x4`); a collapsed child has
`g.sy == 1` and no separator row, so `stack.sy == (N-1) + expanded.sy`, and its single
row is the pane's title drawn in border style. A flag rather than a new `layout_type`:
every `switch (lc->type)` stays byte-identical (a new type means eleven new cases and an
audit of every `if LEFTRIGHT else` chain) and only the arithmetic gets a special case,
the `LAYOUT_CELL_FLOATING` precedent. Helpers `layout_cell_is_stack`,
`layout_cell_is_collapsed`, `layout_stack_expanded(stack)`, `layout_stack_expand(w, wp)`.

- `layout.c`: `layout_fix_offsets1` adds no `+1` between stack children;
  `layout_fix_panes` copies offsets and `continue`s on collapsed cells (the pty keeps its
  last size, as zoom does; `window_pane_is_visible` returns 0 for them, callers in
  `redraw.c`, `window.c`, `select.c` audited); `layout_resize_adjust` gives all `change`
  to the expanded child; `layout_set_size_check` and `layout_resize_child_cells` keep
  collapsed at 1 and give the expanded `size-(N-1)`; `layout_resize_pane` on a stack
  child resizes the stack against its neighbours; `layout_split_pane` treats a plain
  split of a stack child as a split of the stack, and a new `SPAWN_STACK` inserts a leaf
  into the stack (the new pane expanded) or wraps the cell in a stack node with
  `layout_replace_with_node`; `layout_destroy_cell` gives one row to the expanded when a
  collapsed child leaves, promotes the neighbour when the expanded leaves, and the
  existing one-child collapse frees the node; `layout_spread_cell` returns 0 for a stack;
  presets flatten stacks (`layout_free(w, 1)` clears the flags); zoom is unchanged, and
  `window_set_active_pane` is where `layout_stack_expand` hooks, so selecting or zooming
  a collapsed pane expands it; `window_pane_get_pane_status` returns `PANE_STATUS_TOP`
  for a collapsed pane so the title machinery engages. Floats are TAILQ siblings, so a
  float created from a stack child sits inside the node: the helpers skip floating
  children.
- `custom.c`: `(`...`)` for a stack node, children in order, collapsed at `sy 1`;
  checksum unchanged; `layout_check` sums `sy` without `+1`, requires leaves and all but
  one `sy == 1`; `layout_assign` sets `COLLAPSED` on the others. Upstream tmux rejects
  `(` with "invalid layout", a clean failure recorded in `docs/SYNCING.md`. The `<...>`
  float round-trip in `layout_parse` is fixed in the same change.
- Drawing over pane-border-status: `redraw_mark_pane` marks the collapsed row as border
  then status, `redraw_pane_status_line` returns `wp->yoff` for it, `pane-border-format`
  with `#{pane_collapsed_flag}` renders the title, mouse control numbers come for free;
  `window_get_active_at` gets a pass over collapsed rows so a click lands on the stack.
- Commands: `split-window -S` and `new-pane -S` (`SPAWN_STACK`), `join-pane -S`,
  `select-pane` expands (no flag), `select-pane -S` cycles within the stack,
  `resize-pane` on a stack child resizes the stack, `swap-pane` keeps the expanded slot.
  Formats `pane_stacked_flag`, `pane_collapsed_flag`, `pane_stack_index`,
  `pane_stack_size`, `window_stacks`. No new option. Keys in `etc/termo.conf` (`M-s`
  split into stack, `M-S` cycle) and a "Stack" entry in the pane menu.

### 8.11 Shell integration (added 2026-09-15)

termo provisions the OSC 133 hooks in the shells it spawns, the way Kitty and Ghostty do,
because neither terminal's hooks survive into a pane (their scripts key on the
terminal's `TERM` and both document that tmux gets nothing; nicm in tmux#5237 points at
tmux's own builtin support as the path).

- Scripts in `runtime/shell/`, installed with the runtime (`install_subdir('runtime')`
  leaves the `luajit_dep.found()` block in `meson.build`). `bash/termo.bash`: guarded on
  an interactive shell and `TERMO_SHELL_INTEGRATION`; when injected through `ENV` it
  unsets the injection variables, restores `ENV`, leaves POSIX mode and re-sources the
  normal startup files (login branch on `shopt -q login_shell`, Ghostty's order); bash
  4.4 for `PS0`; `A`/`B` around `PS1`, `C` in `PS0`, `D;$?` in `PROMPT_COMMAND`.
  `zsh/.zshenv` restores `ZDOTDIR` from `TERMO_ZSH_ZDOTDIR`, sources the user's `.zshenv`
  and defers setup to the first `precmd` so it runs after `.zshrc`; `precmd` emits
  `D;$?` and wraps `PS1` with `A`/`B`, `preexec` emits `C`.
  `fish/vendor_conf.d/termo-shell-integration.fish` restores `XDG_DATA_DIRS` and hooks
  `fish_prompt` (`A`), `fish_preexec` (`C`), `fish_postexec` (`D;$status`). Every script
  returns early when `PS1` already contains `133;` or Kitty's or Ghostty's state
  variables exist, so hand-sourced hooks are not doubled.
- C: one static `spawn_shell_integration(new_wp, child)` in `src/core/spawn.c`, on the
  path where the pane runs `default-shell` with no command: basename of `new_wp->shell`
  in `{bash, zsh, fish}`, `/bin/bash` skipped on macOS (Apple's 3.2); sets
  `TERMO_SHELL_INTEGRATION=1`, `TERMO_SHELL_DIR`, and per shell `ENV` (saving the old
  one in `TERMO_BASH_ENV`, `TERMO_BASH_INJECT=1`, `--posix` added to the `execl`),
  `ZDOTDIR` (old in `TERMO_ZSH_ZDOTDIR`) or `XDG_DATA_DIRS` (old in
  `TERMO_FISH_XDG_DATA_DIR`), each only when the script is readable. `default-command`
  and `split-window 'cmd'` are untouched. `termo_runtime_dir()` in `src/core/util.c`
  replaces the static in `src/lua/runtime.c` so both the Lua runtime and the spawn path
  resolve `TERMO_RUNTIME` or the installed `datadir` the same way.
- Option `shell-integration`: session scope, CHOICE `off|detect|bash|zsh|fish`, default
  `off` (rule 3); `etc/termo.conf` sets `detect`. Per pane or session escape hatch:
  `TERMO_SHELL_INTEGRATION=0` in the environment (`set-environment`, `new-window -e`).
- Forwarding: `input_osc_133` keeps swallowing the marks as tmux does; nothing reaches
  the outer terminal in this item.

### 8.12 Layouts as data and swap layouts (added 2026-09-15)

`termo.layout.apply(table)` accepts, next to today's `{ dir, ... }` tree, a window entry
of the §8.8 format (name, `layout` string, panes with `cwd` and `cmd`);
`termo.layout.dump(window)` returns one. `termo.layout.save(name)` and `load(name)` keep
them in `~/.config/termo/layouts/<name>.json`; `termo.layout.swap()` cycles through the
saved layouts whose pane count matches the window, over `select-layout` with the stored
string. No option: the directory is the list.

## 9. Auxiliary executables (out of process)

All in `crates/` when the first one exists (intent decision 2), talking to the server
through control mode (`termo -C attach` on a pipe pair, the `Control` protocol of the
facts) and `run-lua -j`; none links `libtermo`.

- **`termo-mcp`**: an MCP server (stdio and streamable HTTP transports) exposing tools
  `list_sessions/windows/panes`, `read_pane` (with `-e` and a line range), `send_keys`,
  `run_command`, `wait_for` (`pane_command_state` transitions, `pane-command-finished`
  with exit code, output regex), `split`, `new_window`, `capture_command_output` (§8.2's
  mark ranges), plus resources for pane screens and notifications; one control-mode
  client per MCP session; `%output` decoded from the octal escapes. Rust, `tokio`,
  `serde_json`; the schema is the JSON the tools take and return.
- **`termo attach quic://host[:port]`**: a client-side transport (`quinn`) that carries the
  imsg client protocol over one bidi stream and pane output over per-pane streams, keyed
  by a session id + passkey issued by `termo-quicd` on the host (a daemon that accepts
  QUIC and speaks the Unix socket to the local server); reconnect replays unacknowledged
  output from the daemon's ring. Design only in this item; the plan schedules it last.
- **`termo record`/`termo stream`**: `pipe-pane` into an asciicast v3 writer (header from
  `#{pane_width}`/`#{pane_height}`, `o` events, `r` on `pane-resized`, `m` markers on
  `pane-shell-prompt` with the command text), `stream` over WebSocket to a viewer. Small
  Rust binary; replay through `send-keys -l` into a pane runs the parser without a pty.
- **Web bridge**: `termo-web`, a WebSocket server serving an xterm.js page that proxies a
  control-mode client; after `termo-mcp`, sharing its control-mode codec crate.

## 10. `nucleo`

`src/core/fuzzy.c` keeps its API; `termo.api.fuzzy` and `window/switch.c` call
`fuzzy_match`. Under `HAVE_RUST`, `fuzzy_match` is implemented by `src/rs/ffi/fuzzy.rs`
over `nucleo-matcher` (`Pattern::parse` with fzf syntax, `Matcher` with Unicode
normalisation), returning the same `bitstr_t` of matched columns and score; the
differential fuzzer compares match sets, not scores (scoring differs by design and the
change is documented). This adds `nucleo-matcher` to the allowlist (it depends on
`memchr` and `unicode-segmentation`, both already vendored).

## 11. Documentation and repo conventions

`CLAUDE.md`: Rust conventions section (lint set, `SAFETY:`, no `static mut`, the ABI
rules, `just rs-lint`), the crate layout, the differential-fuzz rule ("a Rust module
lands with its C twin still compiled and a fuzzer comparing them"), the `-Drust` option.
`docs/crates.md`: the allowlist with versions and review notes. `docs/SYNCING.md`: the
ported-by-hand rule for `utf8/`, the grid accessor refactor, copy-mode additions.
`docs/plugins.md`: manifest schema, lockfile, native plugins. `docs/man/termo.1`: `-S`,
new `-X` commands, `capture-pane -M`, the new options (`unicode-width-cjk`,
`agent-idle-time`, `sandbox-profile`, `resurrect`, `resurrect-commands`,
`shell-integration`, the `float-*` options), `client-key-table-changed`, `server-exit`,
mode 2027, `split-window -S` and the other stack flags, the `(...)` layout string.
`docs/api.md` regenerated for the new `termo.api` functions (`notify`, `sandbox`).

## Verification

- `meson setup build -Drust=enabled` fails below rustc 1.98 with the message; succeeds
  with 1.98 and produces `libtermo_rs.a` linked into `termo`; `-Drust=disabled` builds and
  passes every suite on gcc-14, clang-20, Apple clang, Alpine and FreeBSD.
- `just bench` produces `build/bench/<sha>.json` with the seven scenarios in both
  measurements; the C fixes of §2 show their ratios in their commit messages; every Rust
  module lands with `just bench --compare` within 10% on all scenarios.
- `tests/unit/test_utf8.c` and `test_grid.c` pass unchanged against the Rust modules; the
  differential fuzzers run 2000 iterations in `meson test --suite fuzz` and 10 minutes
  nightly with no divergence; `cargo +nightly miri test` green in `src/rs/`.
- `meson test` and `just upstream-regress` green with and without Rust, under ASan and
  UBSan on the C side.
- Mode 2027: an e2e case with a fixture emitting `DECSET 2027` and a ZWJ sequence asserts
  one cell of width 2 in `capture-pane -e` and the cursor position reported by `CSI 6 n`;
  the same sequence with the mode off asserts today's cells.
- Scrollback memory: the `resize` and `scroll` scenarios report server peak RSS; the grid
  module lands with RSS after 50k lines of the `utf8` payload below 40% of the C build's
  (Ghostty measured 6%; the gate is deliberately loose).
- Product: Lua specs for `mode`, `command`, `pack` (lockfile round trip, version range),
  `health`, `session`; e2e for modes and the hint bar, `:` user commands, text objects and
  prompt jumps, sandboxed pane on Linux, `client-key-table-changed` in control mode.
- `clang-tidy` job renamed `lint` runs clang-tidy, `cargo fmt --check` and
  `cargo clippy -- -D warnings`; nightly `rust-verify` and the ASan-instrumented fuzz run
  are green on the first dispatch.
- Added 2026-09-15: Lua specs for `pack` (range, lockfile round trip, confirm on install
  and update, `del` and `clean`, a dependency loads first, events, every lazy trigger),
  `session` (a two-window session with layouts and cwds round-trips; `server-exit` fires
  on `kill-server` and the file is written), `layout` (dump/apply round trip equals
  `#{window_layout}`; swap cycles two saved layouts); e2e: a server started with a fixture
  `init.lua` restores a saved session before `attach` succeeds, `commands` mode re-sends
  an allowlisted command, `screen` mode shows the captured text; a bash, zsh or fish
  pane (each skipped when absent) sets `#{pane_last_prompt_time}` and reports the exit
  status of `false` in `capture-pane -M`, `ZDOTDIR` is restored, the option off leaves
  the environment untouched, a non-shell command is not injected; a float created
  without flags gets the option sizes; the stack cases named in the plan's row 24; the
  hint bar stays with `@hints=always` and disappears with `mode`; a `bind -n` root key
  does not fire inside a mode.
