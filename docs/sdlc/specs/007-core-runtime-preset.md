# Spec 007: core fixes, generic runtime and the `modal` preset

- Status: Approved 2026-09-30
- Intent: [`intent/007-core-runtime-preset.md`](../intent/007-core-runtime-preset.md)
- Plan: [`plan/007-core-runtime-preset.md`](../plan/007-core-runtime-preset.md)

## Facts the design rests on

Read on 2026-09-30 at `b8fb58f1`.

- `server_client_key_callback` (`src/server/client.c`): when a binding is found and it does
  not repeat, the client is reset to the default table (`server_client_set_key_table(c,
  NULL)`) before `key_bindings_dispatch` runs it. An unbound key tries `Any` in the same
  table; if there is none and the table is not the default one, the client is reset to root
  and the key is looked up again there. So a mode that wants to stay has to switch back in
  every binding, and a mouse event in a mode either hits `Any` or leaves the mode.
- `server_client_set_key_table` fires `client-key-table-changed` only when the name
  changes, so the reset and the switch back are two real notifications.
- `mode.lua` appends `; switch-client -T mode-<name>` to every non-`once` key and binds
  `Any` to `switch-client -T mode-<name>`; `mode.line` lists `spec.keys` sorted by key;
  `mode.lock` sets `prefix None` on the session and binds one root key to unlock.
- `palette.add` appends; nothing deduplicates. `termo.on` returns an id and `termo.off(id)`
  removes it; there are no groups.
- `api_popup` (`src/lua/ui.c`) reads `w`, `h`, `x`, `y` with `ui_number`, integers only.
  `popup_cb` and `menu_cb` call `on_close` from inside the overlay's free, with the client
  held (`api_hold`), so a popup opened there is replaced by the clear that follows.
- `api_client` resolves: the named client, the running item's client, else the most recently
  active attached client. `api_eval` passes the item's client only, so from a timer or a
  `system` callback `#{client_*}` expands empty while `ui.popup` picks the most recent client.
- Floating panes are layout cells with `LAYOUT_CELL_FLOATING` and a place in `w->z_index`;
  `move-pane -P <position>` (`src/cmd/pane/join.c`) moves or restacks them. Collapsed stack
  children are already invisible through `window_pane_is_visible`, used at 26 call sites
  (drawing, mouse, selection), and keep their pty size.
- `termo.pack.load(dir)` loads a plugin from a directory with a `termo.json`, outside
  `setup` and the lockfile. The runtime directory is `termo_lua_runtime_dir()` in C
  (`TERMO_RUNTIME` or the installed path) and is not exposed to Lua.

## 1. Sticky key tables (dispatch)

A server array option `sticky-key-tables` lists table names. For a client whose current
table is in it:

- a bound key runs and the client stays in the table (no reset before the dispatch), unless
  the binding itself switches tables;
- an unbound mouse event (`KEYC_IS_MOUSE`) is looked up in `root` and dispatched from there,
  or forwarded to the pane, without changing the client's table;
- any other unbound key without an `Any` binding is swallowed and the table is kept;
- the prefix key keeps today's precedence and leaves the table, as 006 axis 9 decided.

Nothing changes for tables not in the list, so upstream semantics hold for every test run
with `-f /dev/null`. The change is two conditions in `server_client_key_callback` plus the
option entry; it is recorded in `docs/SYNCING.md` as a dispatch divergence with the rule
that upstream changes to that function are ported by hand.

`mode.define` adds the table to `sticky-key-tables` (a new `sticky = false` spec field
opts out, for one-shot tables that cancel on any key), stops appending `switch-client` to
its keys and stops binding `Any`. `client-key-table-changed` then fires once on entry and
once on exit.

## 2. Reload and handler groups

- `termo.augroup(name, { clear = true })` returns a group; `termo.on(event, fn,
  { group = g })` records the id in it; `clear` removes every handler of the group. Lua only,
  in `init.lua`, over `api.on`/`api.off`.
- `palette.add(name, action, opts)` replaces an entry with the same name in place.
- `termo.reload()` clears every group created with `clear = true`, removes from
  `package.loaded` the modules whose file is under the user config directory, and runs the
  user config file again through `run-lua -f`. The config path comes from a new
  `termo.api.config_file()` (the file `cfg_select_files` chose, or nil under `-f`).

## 3. UI and callback context

- `popup` and `menu` sizes accept a number (cells) or a string `"N%"` of the client's size;
  `x`/`y` default to centred as today. One helper, `ui_size`, replaces `ui_number` for the
  four fields.
- `on_close` of a popup and a menu is called from an `event_once` with a zero timeout after
  the overlay is freed, not from inside the free. The client is referenced until then.
- A callback context client: `termo.defer`, `termo.timer`, `termo.system` and the deferred
  `on_close` capture the client current at creation (the item's, else the held one) with a
  reference, and set it as the context client while the callback runs. `api_client` and
  `api_eval` use the context client when there is no item. The command context rule does
  not change: a timer or `system` callback still drains the global queue.

## 4. `mode.lock` for root-key leaders

`termo.mode.lock(unlock_key, { pass = { "C-a", ... } })` also rebinds each `pass` key in
root to `send-keys <key>` and restores the previous root bindings on `unlock`. Without
`pass` it behaves as today.

## 5. Modes: grouped hints, counts, key sheet

- `mode.define(name, spec)` accepts `spec.groups = { { title, { key, ... } }, ... }` naming
  keys of `spec.keys`, and a key entry may carry `enter = "<mode>"` meaning it enters
  another mode. `mode.line` draws groups separated by `│`, keys that enter a mode as a pill
  in that mode's `color` (a new spec field, a style colour), others as key and label. A
  spec without `groups` keeps today's line.
- Counts: a key entry with `count = true` receives the pending count: a string rhs is
  repeated (capped at 99), a function receives `ev.count`. `mode.define` binds `0`-`9` in a
  table that has any counted key; a count expires after `termo.mode.count_timeout` (3 s)
  and `#{mode_count}` shows it.
- Key sheet: `termo.mode.sheet(name)` opens a `termo.ui.menu` listing the mode's groups
  (titles as disabled items, then each key as an item with the key as its shortcut), and
  choosing an item runs that key's rhs in the mode. `spec.sheet_delay` (ms, off by default)
  opens it automatically when no key is pressed after entering the mode; any key cancels
  the wait. `?` is bound to it when `spec.help ~= false`. No external process.

## 6. Hideable floats

- A floating cell gains `LAYOUT_CELL_HIDDEN`. `window_pane_is_visible` returns false for a
  pane whose cell has it, so drawing, mouse and selection skip it through the existing call
  sites; its geometry and pty size are kept because the cell is not touched.
- `move-pane -P hide` and `move-pane -P show` set and clear it on the source pane. Hiding
  the active pane selects the last visible pane first. `select-pane -t` on a hidden float
  shows it.
- Formats `pane_hidden_flag` and `window_hidden_floats` (count).
- `termo.float.hide_all()`, `show_all()` and `toggle_all()` (hide the visible floats of the
  window; if none, show the hidden ones; if none, create one), the dotfiles' behaviour.

## 7. Runtime modules

- `termo.pick(spec)` (`pick.lua`): `{ title, prompt, items | source, preview, expect,
  delimiter, with_nth, on_choice(key, line) }`. With `fzf` on `PATH` it runs fzf in a popup
  (the dotfiles' `pick.lua`, colours from `termo.theme` when set); without it, `items` or
  the lines of `source` go to a `termo.ui.menu` filtered by `termo.fuzzy` through a prompt,
  without preview. `expect` keys are fzf-only and documented so.
- `termo.session.picker(opts)` (`sessionizer.lua`, moved from 006 row 15): live sessions,
  saved ones from `termo.session.list()`, and directories under `opts.roots` to
  `opts.depth` (a `find` through `termo.system`), in one `termo.pick`; choosing goes to,
  revives or creates. Rename, kill and forget are `expect` keys under fzf and menu items
  otherwise.
- `termo.theme` (`theme.lua`): a palette is a table of tokens `bg fg muted surface accent
  red green yellow blue magenta cyan`; `apply(palette)` sets `status-style`,
  `pane-border-style`, `pane-active-border-style`, `message-style`,
  `message-command-style`, `mode-style`, `menu-style`, `menu-selected-style`,
  `menu-border-style` and `popup-border-style`, and remembers the palette for the modules
  above; `set(name)` loads `termo.themes.<name>`; `:colorscheme [name]`. Shipped:
  `kanagawa`, `catppuccin`, `gruvbox`.
- `termo.health` (`health.lua`, moved from 006 row 15): `termo health` user command and a
  palette entry; sections from 006 row 15 (terminal features, clipboard, Lua budget,
  plugins and lockfile, `resurrect`, shell integration when present, `+rust`) plus
  `termo.health.register(title, fn)` where `fn(report)` calls `report.ok/warn/bad(text,
  note)`. Shown in a `less -R` popup, or a message list without `less`.
- `#{git_branch}` registered by the runtime (walks up from `pane_current_path` reading
  `.git/HEAD`, no process); `termo.command.vim()` defines `vs`, `q`, `only`, `tabnew`,
  `tabclose`, `b` and the `sp` alias.

## 8. The `modal` preset

- Lives in `runtime/pack/dist/opt/modal/` (`termo.json`, `lua/modal.lua`, `lua/modal/*.lua`).
  `termo.pack.load("modal")` resolves a bare name that is not a registered plugin to
  `<runtime>/pack/dist/opt/<name>`; `termo.api.runtime_dir()` exposes the directory.
- `require("modal").setup(opts)`, every choice an option: `enter` (default `C-a`),
  `leader` (default `Space`), `labels` (a table overriding hint and sheet texts), `sheet_delay`,
  `mouse`, `lock_key`. It defines INSERT as root with `enter` into NORMAL, NORMAL with the
  `Ctrl-w` keys and counts, the leader table, the `g` and `y` one-shot tables, TAB and
  RESIZE, VISUAL as copy mode with the return to the mode it came from, LOCKED through
  `mode.lock{ pass = { enter } }`, and the hints and sheets through §5.
- Built only on §1 to §7. Off unless loaded; nothing in `defaults.lua`.

## Verification

- §1: e2e `test_lua_ui.py` on a `Pty` with a mode defined: `%client-key-table-changed`
  arrives once on entry and zero times for three mode keys; a click in a pane inside the
  mode selects it and the mode stays; a `-f /dev/null` server behaves as upstream (existing
  e2e and `just upstream-regress` green).
- §2 to §5, §7: Lua specs, one per new function (the runner enforces it), plus e2e for what
  a user sees: a popup opened from `on_close` is on screen, `w = "80%"` gives the width,
  `#{client_name}` from `termo.defer` is the creating client, the key sheet runs a key.
- §6: unit `test_layout.c` (hidden cell keeps geometry, not visible) and e2e
  `test_layout.py` (hide, the process survives, show restores the geometry, a hidden float
  is not drawn in `nest()`).
- §8: e2e `test_modal.py`: `enter` → NORMAL → `Space o` opens the picker; `setup{ enter =
  "C-g", leader = "," }` moves both.
