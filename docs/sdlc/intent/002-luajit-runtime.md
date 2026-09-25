# Intent: 002 — Embedded LuaJIT Runtime & termo.api Foundation

- **Author**: Antigravity & User
- **Status**: Approved 2026-09-07 (plan in `plan/002-luajit.md`; runs after plan 003)
- **Lifecycle Stage**: Stage 1 (Intent)
- **Target Release**: termo v0.2.0

---

## 1. Problem Statement & Motivation

Existing terminal multiplexers suffer from two extremes in extensibility:
- **tmux**: Relies exclusively on brittle, unhygienic POSIX shell scripts (`run-shell`, TPM). Plugins run as separate fork/exec processes, causing visible UI stutter, process overhead, and quoting/injection vulnerabilities.
- **Zellij**: Embeds a WebAssembly (WASI) runtime. Plugins are multi-megabyte compiled `.wasm` binaries requiring a full Rust toolchain, sluggish startup times, high memory usage, and clumsy serialized IPC.

**termo's solution**: The Neovim architectural pattern. Embed **LuaJIT 2.1** directly inside the server process. Microsecond in-process execution, <2 MB memory footprint, native C FFI access, and ergonomic scripting for user configuration (`init.lua`) and plugin authors.

---

## 2. Goals & Invariants

### In-Scope
1. **Server-Embedded LuaJIT**: A dedicated `lua_State` owned by the server process, cleanly initialized at startup and closed at shutdown.
2. **Core `termo.api` Module**:
   - `termo.api.version()`: Returns project version string.
   - `termo.api.cmd(str)`: Synchronously/asynchronously execute internal commands with return codes and output.
   - `termo.api.get_option(name)` / `set_option(name, value)`: Inspect and modify server/session/window options directly.
3. **Declarative Configuration Loader (`init.lua`)**:
   - Auto-load `~/.config/termo/init.lua` if present, alongside legacy `termo.conf` / `tmux.conf`.
4. **Interactive CLI / Command**:
   - A native command `run-lua` (or `lua-eval`) allowing users and keybindings to execute Lua code from the command line or bindings.
5. **Zero Memory Leaks**: Strict ASAN/UBSAN cleanliness when starting, executing Lua scripts, garbage collecting, and exiting.

### Out-of-Scope for this Phase
- Raycast plugin manifests (`termo.json`) -> Stage 3.
- `termopack` package manager -> Stage 3.
- Floating pane APIs -> Phase 3.

---

## 3. Key Constraints
- **Zero-WASM Guarantee**: Extensibility is 100% LuaJIT in-process or JSON-RPC out-of-process.
- **Never Break Regress**: Upstream regression tests in `tests/regress/` must remain 100% green.
