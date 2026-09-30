# Requirements & Design Spec: termo Architecture & Platform

**Derived from**: [`intent/001-termo-foundation.md`](../intent/001-termo-foundation.md)  
**Status**: Proposed / Review Ready  
**Owner**: willy / wandresrb  

---

## 1. Functional Requirements

### FR-1: Build & Quality Engineering (Meson + Ninja)
- **FR-1.1**: The project must build cleanly with `meson setup build && ninja -C build` in under 5 seconds on developer machines.
- **FR-1.2**: Native dependency resolution for `libevent`, `ncursesw`, `luajit`, and optionally `utf8proc`.
- **FR-1.3**: First-class support for memory and undefined behavior sanitizers (`-Db_sanitize=address,undefined`).
- **FR-1.4**: Integrated test runner (`meson test`) executing:
  - Unit tests for isolated C modules.
  - Lua API test suites.
  - Existing regression test suite (`regress/*.sh`).

### FR-2: Sane Defaults (Zero-Configuration Out-of-the-Box)
- **FR-2.1**: `mouse on` enabled by default.
- **FR-2.2**: 24-bit TrueColor (`RGB` / `Tc`) and 256 colors enabled automatically without terminal override hacks.
- **FR-2.3**: OSC 52 clipboard integration enabled by default.
- **FR-2.4**: Vi-mode key table active by default for scrollback and copy-mode.
- **FR-2.5**: Full backwards compatibility with existing `.tmux.conf` configuration files.

### FR-3: Embedded Scripting & Extension Engine (LuaJIT)
- **FR-3.1**: LuaJIT linked into the server binary (`termo-server` / `tmux.c`).
- **FR-3.2**: Configuration bootstrap loading `~/.config/termo/init.lua` (if present) before or in place of `.tmux.conf`.
- **FR-3.3**: C <-> Lua FFI API (`termo.api`):
  - `termo.api.cmd(string)`: Execute tmux command pipeline asynchronously.
  - `termo.api.get_option(scope, key)` / `set_option(scope, key, val)`.
  - `termo.api.list_panes()`, `list_windows()`, `list_sessions()`.
  - `termo.on(event, callback)`: Hook into lifecycle events (`pane_focus`, `pane_closed`, `window_created`).

### FR-4: Raycast-Style Plugin Manifests & Native Package Management (`termopack`)
- **FR-4.1**: Plugins declare capabilities via `termo.json` (metadata, commands, arguments, default keys, status widgets).
- **FR-4.2**: Native package manager `termopack`:
  - `termo pack install <git-url | user/repo>`
  - `termo pack update [plugin]`
  - `termo pack list`
  - Packages installed to `~/.local/share/termo/pack/plugins/`.
- **FR-4.3**: Fuzzy Command Palette: Built-in searchable modal listing native commands and plugin commands declared via `termo.json`.

### FR-5: Native First-Class Floating Panes
- **FR-5.1**: `struct window_pane` extended with `PANE_FLOATING` flag, coordinates (`fx, fy, fw, fh`), and z-index.
- **FR-5.2**: Floating panes can be focused, moved, resized, and toggled (`termo toggle-float` / hotkey).
- **FR-5.3**: Ability to convert any tiled pane into a floating pane and vice versa.

### FR-6: Discoverable Modal Navigation & Declarative Layouts
- **FR-6.1**: Dynamic bottom mode bar displaying active mode (`NORMAL`, `PANE`, `RESIZE`, `SCROLL`) and key hints.
- **FR-6.2**: Declarative layouts defined in Lua tables or clean YAML specifications.

---

## 2. Technical Architecture & Component Design

```
┌────────────────────────────────────────────────────────────────────────┐
│                              termo Server                              │
├────────────────────────────────────────────────────────────────────────┤
│                          libevent Event Loop                           │
├───────────────────────────────────┬────────────────────────────────────┤
│ Multiplexing Core (C23)           │ Extension & Scripting Layer        │
│ • Client/Server IPC (Unix Socket) │ • Embedded LuaJIT Runtime          │
│ • Window / Pane State Machine     │ • termo.api FFI Bridge             │
│ • VT100/xterm Terminal Emulation  │ • termo.json Manifest Loader       │
│ • PTY process supervision         │ • termopack Git Package Manager    │
├───────────────────────────────────┴────────────────────────────────────┤
│ Presentation & Layout Engine                                           │
│ • Tiled Panes (layout_cell tree)                                       │
│ • Floating Panes (z-ordered window_pane overlay)                       │
│ • Modal Navigation Hints & Status Line                                 │
│ • Fuzzy Command Palette                                                │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Flagged Areas of Concern & Trade-Offs

| Area | Concern / Risk | Mitigation Strategy |
| :--- | :--- | :--- |
| **LuaJIT & `fork()`** | LuaJIT state across process forks (`spawn.c`, job handling). Forking a process with an active Lua state can cause allocator corruptions if not handled carefully. | Run LuaJIT exclusively in the main server event loop. Worker jobs and child ptys are spawned via standard `forkpty` without inherited Lua VM execution. |
| **Floating Panes vs Layout Cells** | tmux's layout engine (`layout.c`) strictly assumes all panes partition the full window geometry in a binary tree (`struct layout_cell`). | Floating panes bypass the layout tree. They are maintained in a separate `TAILQ_HEAD` (`window.floating_panes`) and rendered during `screen_redraw` as an overlay pass. |
| **Build System Parity** | Keeping Autotools working while introducing Meson could create drift in source lists. | Define source lists in single source of truth or keep Autotools minimal as legacy OpenBSD shim while Meson is primary for termo development. |
| **Sanitizers in CI** | libevent or legacy shims in `compat/` might trigger benign leaks or false positives under ASAN. | Use an `asan.supp` suppression file strictly for known external OS library false positives; zero tolerance for termo code. |

---

## 4. Acceptance Criteria & Verification

1. **Build**: `meson setup build -Db_sanitize=address,undefined && ninja -C build` succeeds with zero compiler warnings or errors.
2. **Regression**: `regress/*.sh` passes 100% green against the built `termo` binary.
3. **Defaults**: Running `termo` with `-f /dev/null` enables mouse scrolling, TrueColor support, and vi-mode copy keys immediately.
4. **Lua Engine**: Executing `termo run-shell "termo.api.cmd('display-message hello')"` or loading `init.lua` executes properly without memory leaks under ASAN.
