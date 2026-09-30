# Intent 007: what the dotfiles prove, split into core, runtime and a preset

- Status: Approved 2026-09-30
- Author: wandresrb
- Evidence: `~/repos/dotfiles/termo/` (`init.lua` and `lua/config/`, 19 modules, 2,670 lines)

## Problem

The product that is used every day is not in this tree. It is a user config of 2,670 lines of
Lua over the 006 runtime: vim-style modes (INSERT as root, `C-a` into NORMAL, a leader on
Space, `g`, `y` and `"` one-shot tables, TAB and RESIZE), a which-key sheet, counts and
registers, a two-row status line with per-mode hints, fzf pickers, a session and project
picker, hideable floats, themes, `:help` and `:checkhealth`. Read module by module, about half
of it is not preference. It works around gaps in the core and the runtime, and its comments
say so:

| Workaround in the dotfiles | Gap it covers |
|---|---|
| `floats.lua` hides a float by `break-pane` into a `_floats` session kept alive by `tail -f /dev/null`, with the geometry in `@float_geom` and the home window in `@float_home` | the core cannot hide a pane; a float loses its geometry when it leaves its window |
| `util.to_normal` rebinds `C-a` in `mode-normal` on every entry; `counts` never clears on a table change; `whichkey` counts table changes to cancel its timer | a binding without `-r` resets the client to `root` after it runs (`server_client_key_callback`), so every key of a mode passes through `root` and fires `client-key-table-changed` twice |
| `mouse.lua` copies root's mouse bindings into every `mode-*` table | `mode.lua` binds `Any` to the mode table, and mouse events are keys, so a mode swallows them |
| `util.on` and `util.reset` track handler ids, empty `termo.palette.entries` and purge `package.loaded` before a reload | `termo.on` has no groups, `palette.add` appends duplicates, there is no reload entry point |
| `statusline` replaces `termo.mode.line`, which lists every key of a table in key order | `mode.define` has no grouped hints and no notion of a key that enters another mode |
| `whichkey` opens a popup running bash to read one key, writes it to a file and re-feeds it with `send-keys -K` | no key sheet in the runtime; a popup cannot pass a key back |
| `help.lua` opens its popup from a `termo.defer(30)` | a popup cannot be opened from another popup's `on_close` (the close runs inside its free) |
| `util.client_size(0.8)` everywhere | `termo.ui.popup` takes cells only (`ui_number` in `src/lua/ui.c`) |
| `client` measured at key time and threaded through every timer and `system` callback | a timer or `system` callback has no client, and `#{client_*}` expands empty there |
| `extras.lock` rebinds root `C-a` and a separate unlock key | `termo.mode.lock` only sets `prefix None`, which does nothing when the leader is a root binding |
| `pick.lua`, `sessions.lua` drive fzf or a bash loop inside a popup and hand the choice back through a temp file | no picker primitive in the runtime |

The other half is generic function that any user of modes wants (a picker, a session and
project switcher, counts, themes as tokens, a health report, `#{git_branch}`) and a layer of
real opinion (which keys, which modes, Spanish labels, Nerd Font icons, `~/repos`, kanagawa).
Today all three live in one private directory, so nobody else gets them, a runtime change can
break them silently, and each gap is paid for again in every config that meets it.

## Outcome

Each line of the dotfiles lands in the layer it belongs to, and the dotfiles shrink to the
opinion that is only this user's:

1. **Core and runtime fixes** remove the workarounds of the table above. Each fix deletes code
   from the dotfiles and comes with the test that proves the gap is closed.
2. **Generic runtime modules** in `runtime/lua/termo/` for what is function, not taste.
3. **An opinionated preset** as a plugin, the LazyVim of termo: the vim-style modal layer as a
   whole, installable, versioned, testable, off unless asked for.
4. **The dotfiles** keep only settings: theme choice, roots, labels, icons, extra keys, `doc/`.

The measure is the size of `lua/config/` after the item: what remains there is preference.

## Axes

### 1. Core and runtime fixes

Enters, one fix per row of the Problem table:
- Hideable floats: hide and show the floats of a window without moving them to another
  session, their processes alive and their geometry kept (Zellij's toggle). C in `layout/`
  and `window/`; `termo.float.toggle()` over it; a count of hidden floats as a format.
- No transient `root`: a key bound in a mode table keeps the client in the table without
  passing through `root`, so `client-key-table-changed` reports only real changes and the
  runtime stops appending `switch-client -T <mode>` to every mode key. Decided
  (2026-09-30): the fix is in the dispatch (`server_client_key_callback`), not a filter on
  the notification; the divergence is recorded in `docs/SYNCING.md` and upstream changes to
  the dispatch are ported by hand.
- `Any` in a mode table does not swallow mouse events; they fall through to `root`'s mouse
  bindings and the mode stays active.
- Reload hygiene: `termo.on` takes a group that can be cleared (`augroup{clear = true}`),
  `palette.add` replaces an entry with the same name, and a `termo.reload()` that re-runs the
  user config with those groups cleared.
- `mode.define` takes grouped hints and marks keys that enter another mode, so the hint bar
  is drawn by the runtime and `mode.line` is not replaced.
- `termo.ui.popup` accepts percentages and centres by default.
- A popup or menu may be opened from another's `on_close`; the core queues it.
- A timer or `system` callback created while a client was current keeps that client for
  `eval`, `cmd` and `ui`.
- `termo.mode.lock` works when the leader is a root binding.

Does not enter: new commands for what a Lua function covers; changes to the key dispatch
beyond the transient `root`.

### 2. Generic runtime modules

Enters, in `runtime/lua/termo/`, each with its spec in `tests/lua/`:
- `termo.pick`: a picker over a list or a shell source, with preview and extra keys; fzf when
  it is on `PATH`, `termo.fuzzy` in a menu when it is not. `pick.lua` is the model.
- A session and project switcher over `termo.session` and `termo.pick`: live, saved and
  project directories in one list, with create, rename, kill and forget, designed from
  `sessions.lua` and `projects.lua`. Decided (2026-09-30): it moves here from 006 row 15.
- Counts in `mode.define` (a key flagged `count` receives the pending number, which expires
  after a delay, and the number shows in the hint bar).
- A which-key sheet in `mode.lua`: shown after a delay on entering a mode, or on `?`, drawn
  from the grouped hints of axis 1, with no bash in between.
- `termo.theme`: a palette of named tokens (`bg`, `fg`, `accent`, `muted`, `surface` and the
  mode colours) applied to every `*-style` option, a few themes shipped, `:colorscheme`.
- The health report (`termo health` with the sections 006 row 15 listed) and
  `termo.health.register()` so the runtime and plugins add sections; the dotfiles' list of
  tools becomes one such section. Decided (2026-09-30): it moves here from 006 row 15.
- `#{git_branch}` read from `.git/HEAD` without a process, and the vim command aliases
  (`:vs`, `:sp`, `:q`, `:only`, `:tabnew`, `:b`) behind one opt-in call.

Does not enter: Nerd Font icons as a requirement; any key binding by default. Compiled-in
defaults stay tmux's and `defaults.lua` does not change in this item.

### 3. The vim-style preset as a plugin

Enters: the modal layer of the dotfiles (INSERT as root, `C-a` into NORMAL with the `Ctrl-w`
keys, the leader tree, the `g` and `y` tables, TAB and RESIZE, visual
through copy mode and the return to NORMAL, LOCKED for a nested termo, the hint sets and
sheets) as a `termopack` plugin with a `termo.json` manifest, built only on the runtime of
axes 1 and 2, with options for the leader and the labels, in English.

This does not contradict 006 axis 9: the mechanism (modes, hints, lock) is shipped runtime;
the preset is one opinion over it, and the 006 amendment already says the mode list is an
example, not a shipped set. Axis 9's warning that a modal system in a plugin dies with its
maintainer is answered by where the preset lives (below).

Decided (2026-09-30): the preset lives in this tree as a bundled opt-in plugin (Neovim's
`runtime/pack/dist/opt/`), under the same CI, loaded by name; its name is `modal`. Every choice it makes is a
`setup()` option in Lua, and the first two are the ones a user changes first, as the leader
is in Neovim: `enter`, the chord that leaves INSERT for NORMAL (it has to be a chord, since
in INSERT every plain key belongs to the pane), and `leader`, the key inside NORMAL that
opens the rest, `Space` by default.

Does not enter: the preset in the compiled-in defaults or in `defaults.lua`; vim registers
(`"a` and its table), neither in the preset nor in the runtime (decided 2026-09-30).

### 4. The dotfiles after

What stays: `slug.lua` and the theme choice, `projects.setup({ roots })`, base-index and
rename options, Spanish labels as preset options, icons, `doc/`, the tool list for health.
The item is done when `lua/config/` is that and the preset's setup call.

## Scope

`runtime/lua/termo/`, `src/lua/{ui,events,timer,api}.c`, `src/server/client.c` (transient
root only), `src/layout/` and `src/window/` for hideable floats, `tests/lua/`, `tests/e2e/`,
`docs/api.md`, `docs/plugins.md`, `docs/example_init.lua`, the preset plugin. The
dotfiles are rewritten against the result, outside this tree.

## Constraints

- A core fix lands before the runtime module that needs it, and each fix removes a named
  workaround from the dotfiles in the same step, so the dotfiles are the acceptance test.
- Runtime modules work without fzf, fd, bat, zoxide or a Nerd Font, and degrade to
  `termo.ui` menus.
- Tests with `-f /dev/null` see no preset and no new default.
- The transient `root` change touches the dispatch upstream keeps rewriting; it is recorded
  in `docs/SYNCING.md` or not made.

## Evals

Per core fix: a unit or e2e case that fails today (a mode key fires one
`%client-key-table-changed`, a click inside a mode reaches the pane, two `palette.add` with
one name leave one entry, `popup{w = "80%"}` has the right width, a popup opens from an
`on_close`, `#{client_name}` from a timer is the creating client, a hidden float keeps its
pid and geometry). Per runtime function: a spec in `tests/lua/`, which the runner already
enforces. For the preset: an e2e module driving INSERT → NORMAL → leader → picker on a `Pty`.
For the item: `lua/config/` line count before and after, in the plan's close.

## Open

- The `enter` chord stays `C-a` for now. Why a multiplexer has to reserve a key at all,
  and which architectures avoid it, is open for research before the spec.
