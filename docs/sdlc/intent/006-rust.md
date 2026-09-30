# Intent 006: Rust where the bytes are untrusted, measured before moved

- Status: Approved 2026-09-09; amended 2026-09-15 (axes 4 and 9, product table)
- Author: wandresrb
- Research: `docs/sdlc/research/006-rust.md`

## Problem

termo's engine is tmux's C: one process, one libevent loop, every object owned by that
thread. The parsers that read bytes written by any program in any pane (VT escape parser,
UTF-8 decoder, grid storage, sixel) are where a memory bug becomes a CVE; tmux's one real
CVE (CVE-2020-27347) was a stack overflow in the SGR parser. The roadmap says hot paths
"never regress in throughput" and names a 10% gate, but no benchmark exists to apply it.
Concurrency is wanted, but the single loop is a design decision, not an accident, and the
Rust terminals that added threads (WezTerm, Alacritty, Zellij) moved the serial section
rather than removing it. The extension layer is LuaJIT and works; Rust's role next to C
and Lua is undefined, and so is the plugin story for native code.

## Outcome

Rust enters termo for two reasons and in this order: to remove the memory-bug class from
the modules that parse untrusted bytes, and to make concurrency a compile-time property
where a measurement shows it pays. C keeps the engine (it is where upstream fixes land),
Lua keeps everything a user writes. This intent fixes the eight axes that the following
plans work through; each axis says what enters, what does not, and what is still open.

## Axes

### 1. Rust in the leaf modules that read untrusted bytes

Enters: VT parser, UTF-8 decoding and width, grid cell storage and reflow, sixel; one
module at a time behind the C ABI that exists today, `#[repr(C)]` mirrors with size checks
on both sides, `extern "C"` with abort-on-panic (Rust 1.81 semantics), the system
allocator so ASan and `malloc_trim` keep seeing the heap. Layering copied from the Linux
kernel: a generated `bindings` crate no one calls directly, a safe abstraction crate, leaf
code that never touches raw bindings, `SAFETY:` on every unsafe block,
`unsafe_op_in_unsafe_fn` and `arithmetic_side_effects` as deny.
Does not enter: the command pipeline, sessions/windows/panes, layouts, tty drawing,
options, formats, modes. Those stay tmux.
Decided: `utf8/` → `grid/` → `input/`, MSRV 1.98 (decisions 3).

### 2. A throughput benchmark before the first module

Enters: a VT benchmark driven through a pty against `build/termo` (vtebench/termbench
scenarios: ASCII, UTF-8 heavy, SGR heavy, scrolling, resize), reporting where time splits
between the parser stage and the apply/draw stage; the roadmap's 10% gate becomes a
comparison of two runs of it. Ghostty's data (7x in the parser stage, 2x in a real `cat`)
is why the split matters more than the total.
Does not enter: a CI gate on absolute numbers (runners are noisy); instruction-count
microbenchmarks as a substitute for the pty run.
Open: tool choice, scenarios, where results live.

### 3. Concurrency only where it is sound, typed by Rust

Enters, after axis 2 says it pays: parse off the loop thread and apply on it (WezTerm and
Alacritty's shape: a worker owns the pty read and the parser, emits typed actions, the loop
thread applies them in order, wakeup through a pipe the loop watches); readers by snapshot
(`arc-swap`/`left-right`) for scrollback search and control-mode consumers; scoped data
parallelism over data that is provably frozen (a private copy, a resize window).
Does not enter: an actor per pane, thread-per-core, io_uring runtimes, locks around the
object graph, `evthread` on libevent. The serial section (grid write plus redraw to N
clients) is accepted as serial.
Open: whether the parser worker is per pane or a pool; how the budget for Lua callbacks
interacts with a worker's wakeups.

### 4. `termopack` takes `vim.pack`'s design, with a lazy layer over its `load` seam

Enters: a `version` field (branch, tag, commit, semver range), a lockfile in the config
directory that reproduces a machine and reverts an update, `@pack-changed` events on the
event bus, a confirmation step before install and before `update()`, a plugin removed from
the list stays on disk inactive until `del()` or `clean()`, a `dependencies` field, Git
only; and, as a second phase, lazy loading as a layer over `setup`'s `load` option
(`vim.pack`'s own seam: `load` is `true`, `false` or a function): triggers `event`, `keys`,
`cmd`, `cond`, `lazy`, plus termo's own `mode` and `format`, with the actions declared in
the manifest so a stub loads the plugin and runs the declared action instead of re-feeding
a key.
Does not enter: a registry, non-Git sources, TPM plugins (`*.tmux` scripts without a
manifest), a startup-time argument for lazy: the server starts once, so the value of lazy
is conditions and isolation, and `load_ms` per plugin is measured from day one to keep
that honest.
Decided (2026-09-15): the entry point stays `termo.pack.setup`, the runtime's convention
for a one-time declaration (`hints.setup`, `palette.setup`, `float.setup`) and TPM's
mental model of a list in the config; `add` was rejected because it implies incremental
calls from anywhere. Lockfile `termo-pack-lock.json` next to `init.lua`. Install asks on
the client that starts the server, or on the first attach when none is present.

### 5. Native plugins through LuaJIT `ffi`, with a stated policy

Enters: a plugin may ship a Rust `cdylib` with C signatures and load it with `ffi.load`
(the `nvim-oxi` shape); one e2e test with a tiny cdylib; a `lib` field in the manifest;
the written policy that plugins are trusted code, as in Neovim, and that a panic in
native code aborts the server.
Does not enter: a WebAssembly runtime (roadmap rule 5), a Rust-to-Rust ABI (`abi_stable`),
a second Lua binding layer (`mlua`) in the core.
Open: whether `ffi` stays available to `init.lua` and every plugin (Neovim's answer is yes)
or is gated per plugin.

### 6. Unicode tables in the tree

Enters: width and grapheme data at Unicode 17 (`unicode-width` or equivalent tables)
instead of the host's `wcwidth`/utf8proc, so emoji, regional indicators and Hangul render
the same on every target; the utf8proc option becomes redundant or a fallback.
Does not enter: ICU4X.
Open: keep `utf8proc` as an option for normalisation only.

### 7. Regex: a documented decision, not a silent swap

Enters: the choice between POSIX `regcomp` (leftmost-longest, platform-dependent
backreferences) kept behind the FFI, and the `regex` crate (linear time, no ReDoS,
leftmost-first, no backreferences) with the semantic change written down and covered by
unit cases; the same choice applies to copy-mode search.
Does not enter: two engines behind an option.
Open: which one; the answer probably differs between the `s/` format modifier and
copy-mode search.

### 8. Builtins over plugins, and a health check

Enters: `termo health` in Lua (outer terminal features, clipboard and OSC 52 state, Lua
runtime, plugins and their versions), the `:checkhealth` pattern; the Neovim 0.12 principle
that the tool is complete without plugins (palette, hints, floats, layouts, statusline).
Does not enter: tree-sitter, LSP, anything editor-shaped.

### 9. Modes and motions over termo's objects (evaluated 2026-09-09)

The idea: Neovim's modes and motions applied to termo's objects. **Insert** is the default
(every key goes to the pane, tmux's `root` table); a leader enters **normal**, where termo
consumes every key, so `Esc`, `hjkl`, `:` never reach a Neovim inside the pane; a
**locked** mode for programs that need the leader. On top, a grammar: `c2p` creates two
panes, `>5` resizes by five, `:folder ~/developer` and `:c2p` as user commands with
arguments. Toggleable (`modes on|off`), off in compiled-in defaults (rule 3), on in
`etc/termo.conf`.

Evidence from the field:
- Zellij: modes are its most liked feature; colliding keys "plagued users since inception"
  until 0.41's *unlock-first* preset (leader `Ctrl g`, then mode, then action), which is
  tmux's prefix model. Its newbie-friendliness comes from the status bar listing the keys
  of the current mode and a first-run wizard, not from modes being the default.
- tmux: one active key table per client; an unbound key drops back to `root` (nicm,
  discussion #4679; `bind -T X any switch-client -T X` keeps a table sticky); no counts,
  no pending operator. `tmux-modal` proves a hierarchical modal (`w s j`, `[w]`→`[ws]` in
  the status bar) works entirely in config, without counts or grammar.
- WezTerm: a stack of key tables (`one_shot`, `timeout_milliseconds`, `until_unknown`,
  `replace_current`), active table name in the status bar. `modal.wezterm`, the plugin
  that added Vim modes, was archived in May 2026 when its author moved on: a modal system
  that lives in a plugin dies with its maintainer.
- Neovim: count × operator × motion, counts multiply (`3d2w` = 6), state in `oparg_T`.
- Kakoune/Helix: object first, verb second, the selection visible before it is acted on;
  their argument is that verbs are few and objects complex, and a mistyped `d3w` destroys
  the wrong thing with no feedback. In a multiplexer the objects are on screen, so this
  weighs more than in an editor: `d3w` killing three windows without a preview is a
  footgun.

What the client key dispatch allows (read 2026-09-09, `src/server/client.c`
`server_client_key_callback`, `server_client_set_key_table`; `src/core/key-bindings.c`
`key_bindings_dispatch`): one `key_table` pointer per client, `last_key`, a repeat flag
and timer, no count and no pending operator. The dispatch is a 270-line state machine that
resets the client to the root table silently on three paths (prefix timeout, a binding
without repeat, an unbound key) and swallows a key that was not found after starting in a
non-root table, so a sticky mode never leaks keys to the pane. `server_client_set_key_table`
is ten lines and notifies nothing. Upstream changed the dispatch in 3.4, 3.5 and 3.6.

Decisions:
- Enters, **modes**: insert by default, leader into normal, locked mode; modes and the
  hint bar segment as part of termo's shipped runtime (`runtime/lua/termo/`), not a
  plugin; the mechanism is the existing sticky tables plus `#{client_key_table}`; one C
  addition of three lines, a `client-key-table-changed` notification in
  `server_client_set_key_table`, so Lua sees the silent resets. Nothing in the dispatch
  is touched. Amended 2026-09-15: the hint bar is an option, `hints`, with `always` (a
  second status row while modes are on, showing the current mode's keys or the prefix
  table's) and `mode` (the row appears only inside a mode); every mode table binds `Any`
  to itself so a root binding does not fire inside a mode (nicm's answer in tmux
  discussion #4679, WezTerm's `prevent_fallback`); the prefix key leaves a mode, as the
  dispatch forces the prefix table and returns to root afterwards; locked mode is session
  scope (`prefix None` plus a root binding to unlock), no C.
- Enters, **user commands**: commands defined from Lua with arguments and completion,
  reachable from the `:` prompt and the palette, declarable by plugins in `termo.json`
  (the `nvim_create_user_command` shape; `command-alias` is a macro and cannot take
  arguments). Lua only. `:c2p` is one such command, not a feature by itself.
- Does not enter: normal mode as the default; a fixed Zellij-style set of modes; `.`
  repeat and undo.
- Open, **motions** (count × operator × object): no multiplexer ships it and nobody is
  asking; the objects and verbs are few, the count pays in two or three cases, and
  destructive verbs would need Kakoune's highlighted selection first. It is a candidate to
  prototype in Lua on top of modes at zero core cost, and to design only if it gets used.
- Resolved (2026-09-15): a Lua format callback does know the client it renders for
  (`api_eval` without a target expands the tree being drawn, which carries the client, so
  `#{client_key_table}` inside the callback is per client). What is not per client is the
  number of status lines, a session option; that is why the hint bar is a status row and
  not a per-client overlay (an overlay that passes a key through is destroyed by the
  pass-through, `server/client.c` overlay key handling).
- Open: the leader default (`C-a` increments in Neovim, `C-b` pages, `C-space` is free);
  whether `Esc` or `i` leaves normal.

### 10. Features reachable only with a Rust leaf (all entered 2026-09-09; research §12)

| Feature | Rust piece | Depends on |
|---|---|---|
| Grapheme clusters, mode 2027 negotiated with panes and the outer terminal; multi-codepoint graphemes as one cell; `wcwidth` fallback | `utf8/` + `grid/` designed for it (`unicode-segmentation`, Unicode 17 tables) | axes 1, 6 |
| Scrollback as pages with idle-time compression (Ghostty: 93.9% saving, zero throughput cost, memory returned with `madvise`) | `grid/` history design: pages from a pool, LZ4 on idle, not a `realloc`'d line array | axis 1 |
| `record-pane` / `stream-pane` to asciicast v3 with OSC 133 markers; replay inside a pane through termo's own parser; `avt` as a second differential-fuzz oracle | greenfield, plus the Rust parser for replay | axis 1 for replay only |
| Agent-aware multiplexer: pane states (`command-started`, `command-finished{exit,duration}`, `waiting-for-input`) from OSC 133 and idle heuristics; a `termo-mcp` / JSON-RPC server over control mode; blocker notifications to the outer terminal (OSC 9/777) | greenfield out of process; events in C/Lua | OSC 133 marks (product table) |
| Sandboxed panes: `new-pane -S <profile>` (filesystem allowlist, network off, env scrubbed), Landlock + seccomp on Linux, Seatbelt on macOS, applied between `fork` and `exec` | policy engine and profiles in Rust; one call from the C spawn path | none |
| Roaming reconnect: `termo attach quic://host`, session keyed by id, replay on reconnect, one QUIC stream per forward (`quinn`) | greenfield client-side transport; server state already exists | none |
| `nucleo` as the fuzzy matcher for the palette, scrollback and cross-session search | replaces `fuzzy.c` | none |
| Command blocks: OSC 133 marks stored in the grid; blocks view in copy mode (collapse, copy last output, jump), duration and exit status in the status line | `grid/` marks; copy-mode view is C23; status is Lua | axis 1, OSC 133 |

Order of leverage: the agent-aware server and sandboxed panes have no competitor among
multiplexers and are mostly greenfield; graphemes and paged scrollback are the reason the
grid is designed in Rust rather than ported; recording, QUIC and `nucleo` are cheap once
the toolchain is in the tree; blocks fall out of the grid marks.

## Product: what the field has that tmux does not, and where each piece lives

termo is to tmux what Neovim is to Vim: the contract is kept (commands, formats, hooks,
control mode, config search order, `TMUX`/`TMUX_PANE`), the internals are fair game when
a change is valuable, and the product grows features that only make sense in termo. A
multiplexer draws into the outer terminal's tty, not into pixels: GPU rendering, Vulkan,
font shaping and ligatures belong to Ghostty, Alacritty, WezTerm and Kitty, and termo's job
there is to get out of their way (pass their protocols through, never tear, redraw only
the diff, agree on Unicode widths). The rule for placing a feature: **Lua** if it composes
things the core already exposes; **C23** if it touches the object graph, the tty or the
command engine; **Rust** if it is a new subsystem with no tmux twin or it parses bytes a
program controls.

| Feature | Who has it | Value | Layer | Status |
|---|---|---|---|---|
| Modal UI: named modes (pane, tab, resize, scroll, search, session, locked) with a hint bar, entered by a leader, left with `Esc` | Zellij; WezTerm key tables; Neovim modes | the first thing a Neovim user expects; Zellij's is the reference and its complaint is discoverability and lag | Lua (`mode.lua` over key tables); the hint bar is a status row gated by the `hints` option; the only core touch is the `client-key-table-changed` notification | enters |
| Vim motions in copy mode: `w b e f t % ( )`, text objects (`iw aw i" a( ip`), visual line/block, marks, `n N`, `[[ ]]` to prompts | Neovim; WezTerm copy mode (partial); Kitty pager | copy mode today has word/line jumps and search, no text objects, no prompt jumps | C23 in `window/copy.c` (new `-X` commands: text objects, prompt jump) driven by Lua keymaps; prompt marks come from OSC 133 already parsed | enters |
| Quick select / hints: two-key labels over URLs, paths, hashes, IPs, then yank, open, or paste | Kitty hints kitten; WezTerm quick select; tmux-fingers/thumbs | the most used plugin class in tmux, done out of process with a popup | a screen-reading overlay primitive (`termo.ui.overlay` on a pane's grid, C23) plus Lua matchers; Rust if matching moves to the grid module | enters |
| Shell integration (OSC 133): jump between prompts, select last command output, mark failed commands in the scrollbar, `cwd` per prompt | Kitty, Ghostty, WezTerm, iTerm2; tmux 3.6 parses OSC 133 | turns scrollback into a navigable structure | parser (C23 now, Rust with axis 1) records marks; navigation is copy-mode C plus Lua keymaps; termo provisions the shell hooks for bash, zsh and fish at pane spawn, as Kitty and Ghostty do, behind the `shell-integration` option (2026-09-15: the outer terminal's hooks never reach a pane, so without this the marks exist only for users who configured their shell by hand) | enters |
| Scrollback in the editor: open the pane history in Neovim in a float, at the current line, with prompt marks as folds | Zellij (`EditScrollback`), Kitty | replaces a pager with the editor the user already knows | Lua (`capture-pane -S -` into a file, `termo.float` with `nvim`) | enters |
| Neovim navigation and sync: `C-h/j/k/l` across nvim splits and termo panes, resize likewise, shared clipboard, open a path from termo in the running nvim (`--server`) | vim-tmux-navigator, smart-splits, nvr | the single most installed tmux+nvim pair of plugins | Lua on the termo side (detect nvim in the pane via `pane_current_command` or an OSC handshake) and a small `termo.nvim` plugin; no C | enters |
| Session persistence: serialize sessions, windows, layouts and cwds; restore on server start; save on structural events and on server exit; modes `layout`, `commands` (the foreground command from `ps -ao ppid,args`, behind an allowlist), `screen` (`capture-pane -e` replayed by `cat` before the shell starts, tmux-resurrect's method) | Zellij session serialization; tmux-resurrect/continuum | the second most installed tmux plugin, fragile as a shell script over the CLI | C: the `resurrect` option (session scope, `off|layout|commands|screen`) and a `server-exit` event; Lua: `session.lua` over `list_*`, `#{window_layout}`, `new-session -d`, `select-layout`, `send-keys`. Corrected 2026-09-15: no layout file format exists in the tree; `session.lua` defines one that `termo.layout` also applies | enters |
| Sessionizer: fuzzy pick a project directory, create or switch to its session | tmux-sessionizer, Zellij session manager | daily workflow for developers | Lua on the palette (`termo.fuzzy`, `termo.system("fd")`) | enters |
| Image protocols: Kitty graphics and iTerm2 inline images through the multiplexer, not only sixel | WezTerm mux; Kitty; Ghostty | tmux is the reason images "don't work in tmux"; passthrough with placement tracking is the hard part | parser and tty (C23 now; Rust when `input/` moves); placement state in the grid module | open: passthrough first, placement later |
| Modern terminal protocols: synchronized output (DEC 2026), Kitty keyboard protocol, theme mode 2031, pixel-size queries, OSC 8 hyperlinks, OSC 52, undercurl | all four terminals; tmux 3.4–3.6 has most | tearing-free redraws and correct keys are table stakes | C23 `tty/` and `input/`, cherry-picked from upstream where it exists | enters via upstream sync |
| Layouts as data with swap layouts | Zellij (KDL, swap layouts) | termo has declarative layouts from Lua; swap layouts do not exist | Lua: the session format of the persistence row saved per name under `~/.config/termo/layouts/`, `termo.layout.swap()` over `select-layout` | enters |
| Stacked panes: tiled panes sharing one cell, one expanded, the others collapsed to a title row | Zellij (stacked panes) | Zellij's most visible layout feature after floats; nothing tmux-based has it | C23 in `layout/`: a flag on a `LAYOUT_TOPBOTTOM` node (the `LAYOUT_CELL_FLOATING` precedent), `(...)` in the layout string, drawing over pane-border-status; recorded in `docs/SYNCING.md` | enters |
| Floating panes with defaults: size, position and border as options, default keys | Zellij (floating panes) | the core has floats (z-order, modal, `move-pane -P`) but size and border are per-command flags only and there are no default keys | C23: window options `float-width`, `float-height`, `float-position`, `float-border-style`, `float-border-lines` read by `layout_floating_args_parse`; keys in `etc/termo.conf`; `float.lua` | enters |
| Command palette with plugin-declared commands and arguments | Zellij plugins, VS Code, Raycast | exists; manifests do not declare commands yet | Lua (`termo.json` fields, `palette.lua`) | enters |
| Health check, first-run and welcome screen | Neovim `:checkhealth`; Zellij welcome screen | onboarding and support | Lua | enters |
| Web client: attach to a session from a browser over WebSocket | Zellij web client (0.43); ttyd, gotty | remote access without SSH, pairing, demos | Rust greenfield, out of process: a `termo-web` bridge over control mode with an xterm.js front end; no core change | open, later |
| SSH / TLS domains: attach to a remote multiplexer as if local, with local rendering | WezTerm mux domains | remote panes that feel local | Rust greenfield over control mode (a client-side mux), large | open, later |
| Multiplayer: several users on one session with separate cursors | Zellij (per-client cursors), tmux `server-access` | rare | out |
| Plugin-rendered panes: a plugin owns a pane's content (file picker, status views) | Zellij plugins | needed for a file picker or a git view in a pane | Lua-driven popup/window mode over `termo.ui`; a Rust pane-content API only if Lua cannot keep 60 Hz | open |
| Kittens-style helpers: unicode input, SSH with config sync, diff viewer, hyperlinked grep | Kitty | nice, not core | Lua plugins in `pack/`, not built in | out of core |
| GPU rendering, font shaping, ligatures, cursor trails | Ghostty, Alacritty, WezTerm, Kitty | wrong layer for a multiplexer | none | out |
| libtermo: the terminal model as an embeddable C-ABI library | libghostty-vt | lets editors and tools embed termo's engine | the Rust leaf modules from axis 1 are the seed; a public library is a separate intent | out for now |

Two cross-cutting product decisions this table implies:

- **Compatibility is the contract, not the code.** tmux's regress suite and `termo.conf`
  defaults keep the contract honest; behind it, copy mode gaining text objects, the grid
  storing prompt marks and image placements, or the layout tree learning stacks are
  internal changes made because they are valuable, not refused because upstream did not
  make them. Each such change is recorded in `docs/SYNCING.md` as a divergence.
- **Product speed comes from Lua, product depth from C23, new subsystems from Rust.**
  Most rows above are Lua over primitives that exist; the C23 rows are small, targeted
  additions to copy mode, the overlay primitive and `layout/`; the Rust rows are things
  tmux never had (web bridge, remote domains, image placement in a Rust grid). That split
  is what keeps the core maintainable while the feature bucket grows.

## Out of scope for the whole item

Rewriting the command/session graph in Rust; WASM; Go, JS or Python runtimes; cargo in
the Meson build of the termo binary (decisions 2); replacing Lua; `std::simd` (nightly),
portable SIMD comes from `std::arch` and `memchr`; the regex engine (decisions 5).

## Evals

Per Rust module: differential fuzzing of the C and Rust builds on the existing corpus;
the e2e suite and `just upstream-regress` green; ASan/UBSan clean on the C side with the
Rust staticlib linked uninstrumented (rustc's sanitizers are nightly; GCC's dynamic
`libasan` and rustc's static runtime do not mix); Miri on the Rust unit tests in nightly
CI; the axis-2 benchmark within 10% of the C build. Per Lua axis: a spec in `tests/lua/`
per new function and an e2e test per user-visible behaviour.

## Decisions (2026-09-09)

1. **Benchmark first.** The VT benchmark and the C fixes that need no Rust are the first
   step of the plan; no Rust module lands before the number exists.
2. **Crates.** Several crates only where there is a reason (the `unsafe`-holding bindings
   layer apart from the `#![forbid(unsafe_code)]` leaves); no crate that exists for
   tidiness. The default shape is flat: one crate whose library is the core and whose
   `bin` targets are the auxiliary executables; a workspace with several crates only when
   a reason appears, and CI or the release flow may change this. What links into the
   termo binary goes through Meson without cargo, with vendored dependency-free crates;
   the auxiliary executables (agent server, QUIC attach, streaming, web bridge) live out
   of process, talk to the server over control mode, and may use cargo and large crates.
3. **Order and floor.** `utf8/` → `grid/` → `input/`; `regsub` is no longer the first
   module. MSRV 1.98, edition 2024, rustup pinned in every CI image including Alpine and
   FreeBSD, a `meson setup` probe that fails below the floor.
4. **`ffi` stays.** Plugins are trusted code (Neovim's model); `pack.lua`'s environment
   guards globals, not intent; the wall-time budget is the guarantee.
5. **Regex: pending, not in this batch.** POSIX `regcomp` stays in `s/` and copy mode;
   whether and how to improve it is decided later, on its own.
6. **Rule 6 of the roadmap** becomes: upstream fixes are cherry-picked per release into
   the parts that are still tmux; once `utf8/`, `grid/` and `input/` are Rust, upstream
   changes in those directories are ported by hand and each port is recorded in
   `docs/SYNCING.md`.
7. **Toolchains.** Nightly Rust is allowed only in CI, for verification (`-Zsanitizer` in
   the fuzz job, Miri on the Rust tests); the merge gate, releases and anything a user
   installs are stable 1.98.
8. **One item.** Everything in this intent is item 006; the plan phases it, nothing is
   split into separate intents.
9. **Internal changes and the intake rule.** Changes to copy mode, grid and layout are
   allowed when a product row needs them, never for style, each recorded in
   `docs/SYNCING.md`. termo keeps tracking upstream tmux, and before any feature or
   improvement is brought in, from upstream or from the field, it is evaluated for where
   it belongs: Lua, Rust, C, or a plugin.
