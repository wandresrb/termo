# Intent: termo Foundation — The Neovim of Terminal Multiplexers

**Status**: Draft / Proposed  
**Author**: wandresrb  
**Context**: Next-generation terminal multiplexer bridging tmux and Zellij  

---

## 1. Problem
1. **tmux** has the best terminal core in the industry (20+ years of battle-tested VT100/xterm emulation, rock-solid client-server Unix socket architecture, tiny ~5 MB RAM footprint), but its user experience is frozen in time:
   - Esoteric configuration and command syntax (`cmd-parse.y`).
   - Plugin ecosystem (TPM) relies on brittle shell scripts and `eval`, with zero structured APIs.
   - No native floating panes (only ephemeral popups).
   - Ancient defaults requiring extensive boilerplate just for mouse, TrueColor, and vi-mode.
   - Outdated Autotools build system lacking unit testing, automated sanitizers, or fast incremental feedback.
2. **Zellij** proved modern UX is possible (floating panes, discoverable modal hints, declarative layouts), but made a disastrous architectural decision: **WASM for plugins**:
   - Compiling megabytes of WebAssembly (`wasm32-wasip1`) for simple widgets.
   - Heavy memory overhead and runtime startup latency.
   - High friction for plugin authors (Rust toolchain required, awkward IPC serialization).
   - Alien to the Unix philosophy.

---

## 2. Proposed Outcome
A modern terminal multiplexer named **`termo`**, built directly as a fork of tmux:
1. **Preserve the Core**: Keep the battle-tested C client/server architecture, libevent loop, and terminal emulation intact.
2. **Anti-WASM Plugin System**:
   - **LuaJIT In-Process**: Native C speed, <2 MB RAM, direct C-API (`termo.api.*`), clean `init.lua`.
   - **Raycast-Style Manifests (`termo.json`)**: Self-describing plugins declaring commands, arguments, and widgets, automatically feeding an integrated fuzzy Command Palette.
   - **Native Package Manager (`termopack`)**: Git-based package management inspired by Neovim 0.12's `vimpack` (no TPM shell scripts).
   - **JSON-RPC Unix Socket**: For external daemons/tools in any language.
3. **Zellij-Grade UX**:
   - Sane defaults out-of-the-box (mouse on, TrueColor, OSC 52 clipboard, vi navigation).
   - First-class native floating/scratchpad panes (`struct window_pane` with z-ordering over tiled layout).
   - Discoverable modal navigation bar (contextual hints without losing tmux keybinding muscle memory).
   - Declarative layouts (Lua tables / YAML).
4. **Modern Build & Quality Infrastructure**:
   - Meson + Ninja build pipeline (<1s rebuilds).
   - Automated sanitizers (ASAN/UBSAN).
   - Test pyramid: unit tests + Lua test harness + headless integration tests + 100% pass on `regress/`.
5. **Selective Strangler-Fig Memory Safety**:
   - Selective leaf modules in Rust (UTF-8, grid scrollback, VT escape parser) only where memory safety mitigates CVEs, without touching the complex C graph.

---

## 3. Affected Systems & Scope
- `termo` server and client binaries (`tmux.c`, `server.c`, `client.c`).
- Build system (`meson.build`, `configure.ac`, `Makefile.am`).
- Window & pane data structures (`tmux.h`, `window.c`, `window-panes.c`, `layout.c`).
- Extension layer: LuaJIT embedding and C <-> Lua FFI boundary.
- Test suites: `regress/`, new unit testing harness.

---

## 4. Constraints
1. **Zero-WASM Guarantee**: Under no circumstances will a WebAssembly runtime be embedded. Extensibility is LuaJIT (in-process) or JSON-RPC (out-of-process).
2. **Preserve OpenBSD / Upstream Correctness**: The existing `regress/*.sh` test suite (~130 tests) must remain 100% green.
3. **Hot-Path Performance**: Input parsing, grid cell manipulation, and screen redraws must not suffer latency or throughput regressions.
4. **AI-Native SDLC Compliance**: Every change follows the AI-Native SDLC Playbook (Intent -> Spec -> Plan -> Fast Feedback Loop -> Verified Implementation).

---

## 5. Open Questions
1. Should Autotools (`configure.ac`, `Makefile.am`) be preserved solely as a fallback for niche BSDs while Meson becomes the primary system, or maintained in strict parity?
2. For declarative layouts: should Lua tables be the primary format (e.g. `layout.lua`) with an optional parser for YAML/KDL, or YAML from day one?
