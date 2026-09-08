# termo

> **The Neovim of Terminal Multiplexers** — The missing link between the rock-solid stability of **tmux** and the modern user experience of **Zellij**.

`termo` is a next-generation terminal multiplexer created as a fork of [tmux](https://github.com/tmux/tmux). It modernizes tmux from the inside out: preserving its 20+ years of bulletproof terminal emulation, client-server Unix socket architecture, and unmatched stability, while introducing modern UX, first-class floating panes, modal navigation, and an intuitive extension engine.

---

## Why termo?

- **tmux is rock-solid, but stuck in the past**: Ancient defaults, esoteric configuration syntax, brittle bash-based plugin management (TPM), and no native floating panes.
- **Zellij brought great UX, but failed on plugins**: Zellij introduced welcome innovations (floating panes, discoverable UI modes, layouts), but adopted **WASM for plugins** — resulting in massive binary bloat, heavy memory overhead, Rust compile times, and high developer friction.
- **termo unites the best of both**:
  - **Bedrock Stability**: Built directly on tmux's battle-tested C core and libevent event loop.
  - **Embedded LuaJIT**: Fast, lightweight (<2MB RAM) scripting and configuration (`init.lua`), identical to the Neovim revolution.
  - **Raycast-Inspired Plugin Model**: Self-describing plugins with declarative manifests (`termo.json`) automatically integrated into a native fuzzy Command Palette.
  - **Native Package Manager (`termopack`)**: Built-in Git-based plugin management inspired by Neovim 0.12's package architecture.
  - **First-Class Floating Panes**: Native scratchpads and overlays layered on top of tiled splits.
  - **Discoverable Modal Navigation**: Contextual keybinding hints and optional modal modes (like Zellij) without losing tmux muscle memory.
  - **Modern Build System**: Meson + Ninja for sub-second rebuilds, C23 compiler standard, automated ASAN/UBSAN sanitizers, and a modern Python/Meson test pyramid.

---

## Sane Defaults Out-of-the-Box

The binary's compiled-in defaults are tmux's; what makes termo feel different ships in [`etc/termo.conf`](etc/termo.conf), installed as the system config and loaded before your own. Override any of it in `~/.config/termo/termo.conf`.

`termo` works out-of-the-box with zero configuration required:

| Feature | termo Default | Legacy tmux Default | Why? |
| :--- | :--- | :--- | :--- |
| **Mouse Support** | `on` | `off` | Smooth mouse scrolling, pane selection, and dragging work immediately. |
| **Key Modes** | `vi` | `emacs` | Vi keys (`h`, `j`, `k`, `l`, `/`, `?`) in copy mode and status line. |
| **Copy Mode Keys** | `v` (select), `y` (yank) | `Space`, `Enter` | Matches modern Vi/Neovim clipboard muscle memory. |
| **Scrollback History** | `50,000` lines | `2,000` lines | Modern RAM is plentiful; you won't lose command output. |
| **TrueColor (24-bit)** | `*:256:RGB` enabled | Off / manual config | Full 24-bit RGB colors without complex `terminal-overrides`. |
| **System Clipboard** | OSC 52 enabled (`on`) | Off | Native copy-paste over SSH sessions without external clipboard daemons. |
| **Window Renumbering** | `on` | `off` | Windows automatically re-index sequentially when one is closed. |
| **Focus Events** | `on` | `off` | Editors (Vim/Neovim) receive focus gain/lost events for auto-save. |
| **Escape Latency** | `10ms` | `500ms` | Instant `<Esc>` response in Vim/Neovim without noticeable lag. |

---

## Building & Installation

### Prerequisites
- A C23 compiler: GCC 14+, Clang 20+, or Xcode 26+ (Apple clang 21). `meson setup` checks and says so.
- [Meson](https://mesonbuild.com/) (0.60+) and [Ninja](https://ninja-build.org/)
- `libevent` (2.0+)
- `ncurses` (with wide-character support: `ncursesw`)
- `pkg-config`
- *Optional*: `libutf8proc` (for advanced Unicode width calculation)
- *Optional*: `luajit` (2.1+ for embedded scripting engine)

On macOS (via Homebrew):
```sh
brew install meson ninja pkg-config libevent ncurses luajit utf8proc
```

On Ubuntu / Debian:
```sh
sudo apt-get install -y meson ninja-build pkg-config libevent-dev libncurses-dev libluajit-5.1-dev libutf8proc-dev
```

### Build with Sanitizers (Recommended for Development)
```sh
# Configure build with AddressSanitizer and UndefinedBehaviorSanitizer
meson setup build -Db_sanitize=address,undefined -Dbuildtype=debugoptimized

# Compile in sub-second incremental passes
ninja -C build

# The compiled binary is available at:
./build/termo -V
```

### Running Tests
`termo` uses a multi-tiered test pyramid integrated into Meson and Python 3:

```sh
# Run all test suites (unit + integration + regression)
meson test -C build --verbose

# Run individual suites
meson test -C build --suite unit           # C unit tests, seconds
meson test -C build --suite integration
meson test -C build --suite regress        # the 129 upstream scripts, in parallel
./build/tests/termo-test format            # one unit module directly
```

---

## Scripting with Lua

`~/.config/termo/init.lua` is loaded after `termo.conf`. Everything the config
language does is there as a function, plus what it cannot do: functions on keys,
hooks with their payload, status line variables computed in Lua, menus, popups,
timers and processes that never block the server. The full surface is in
[`docs/api.md`](docs/api.md); a longer example is
[`docs/example_init.lua`](docs/example_init.lua).

```lua
termo.opt.history_limit = 100000                 -- set -g history-limit 100000

termo.keymap.set("|", "split-window -h")          -- bind-key | split-window -h
termo.keymap.set("C-t", function(ev)              -- a function on a key
	termo.ui.message("pressed %s in %s", ev.key, ev.table)
end)

termo.on("window-renamed", function(ev)           -- a hook with its payload
	termo.cmd("display-message 'renamed " .. ev.window .. "'")
end)

termo.format.add("branch", function()             -- #{branch} in any format
	local f = io.open(termo.eval("#{pane_current_path}") .. "/.git/HEAD")
	if not f then return "" end
	local head = f:read("*l") f:close()
	return head:match("refs/heads/(.*)") or head:sub(1, 7)
end)
termo.opt.status_right = "#{branch} %H:%M"

termo.keymap.set("m", function()                  -- a menu
	termo.ui.menu{ title = "Panes", items = {
		{ "Split right", "r", "split-window -h" },
		{ "Kill", "x", function() termo.cmd("kill-pane") end },
	} }
end)
termo.palette.setup{ key = "p" }                  -- fuzzy command palette
```

From outside the server any language gets the same API as JSON:
`termo run-lua -j 'return termo.api.list_panes()'`.

---

## Configuration & Environment

- **Configuration Files** (in load order):
  - `<sysconfdir>/termo/termo.conf` (termo's defaults, `/usr/local/etc/termo/termo.conf` by default)
  - `~/.config/termo/termo.conf`
  - `~/.config/termo/init.lua` (Lua, see below)
  - Legacy fallback: `~/.tmux.conf`
- **Socket Directory**:
  - `/tmp/termo-<uid>/default` (controlled via `TERMO_TMPDIR`)
- **Environment Variables Exported to Panes**:
  - `TERMO`: Socket path, server PID, and session index (`<socket>,<pid>,<session_idx>`).
  - `TERMO_PANE`: Unique pane identifier (e.g. `%0`, `%1`).
  - `TERM_PROGRAM`: `"termo"`.
  - `COLORTERM`: `"truecolor"`.
  - Backward compatibility: `TMUX` and `TMUX_PANE` are also preserved.

---

## Architecture & Roadmap

See [`ROADMAP.md`](ROADMAP.md) and [`docs/sdlc/`](docs/sdlc/) for detailed architectural blueprints, SDLC specifications, and development phases.

---

## License

`termo` is released under the [ISC License](COPYING), matching upstream OpenBSD tmux. Compatibility shims under `src/compat/` are licensed under their respective BSD/MIT licenses.
