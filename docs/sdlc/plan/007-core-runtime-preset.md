# Plan 007: core fixes, generic runtime and the `modal` preset

- Status: Approved 2026-09-30
- Intent: [`intent/007-core-runtime-preset.md`](../intent/007-core-runtime-preset.md)
- Spec: [`specs/007-core-runtime-preset.md`](../specs/007-core-runtime-preset.md)

Steps land as commits on `refactor/to-termo`, each with `meson test` green under ASan and
UBSan. Steps 1 to 6 are the core and runtime fixes, in the order the later steps need them;
7 to 11 the runtime modules; 12 the preset; 13 the dotfiles, outside the tree. Every step
that adds a `termo.api` or `termo.*` function adds its spec in `tests/lua/` and
regenerates `docs/api.md`; every new option or command touches `docs/man/termo.1`.

| Step | Contents | Proof |
|---|---|---|
| 1 | **Sticky key tables** (spec §1). `src/config/options-table.c`: `sticky-key-tables` (server, array of strings, empty). `src/server/client.c` `server_client_key_callback`: a helper `server_client_table_is_sticky(table)`; skip the reset before `key_bindings_dispatch` for a sticky table; on no match in a sticky table, mouse events are looked up in `root` without `server_client_set_key_table`, other keys are swallowed. `runtime/lua/termo/mode.lua`: `define` appends the table to `sticky-key-tables` unless `spec.sticky == false`, no `switch-client` appended, no `Any`. `docs/SYNCING.md`: the dispatch divergence and the porting rule. `docs/man/termo.1` | e2e `test_lua_ui.py`: one `%client-key-table-changed` on entry, none for three mode keys, `Escape` gives one; a `MouseDown1Pane` in a mode selects the pane and `#{client_key_table}` is still the mode; an unbound letter in a mode does not reach the pane (`capture-pane`); a `sticky = false` table returns to root after one key. `mode_spec.lua` updated. `just upstream-regress` full run green; existing e2e green |
| 2 | **Handler groups, palette, reload** (§2). `runtime/lua/termo/init.lua`: `termo.augroup`, `termo.on(event, fn, opts)` with `group`; `palette.lua`: `add` replaces by name; `termo.reload()`; `src/lua/api.c`: `config_file()` from the list `cfg_select_files` produced (stored in `src/config/cfg.c`, nil under `-f`) | Lua specs: a cleared group removes its handlers and leaves others; two `palette.add` with one name leave one entry with the second action; `config_file` is nil under `-f /dev/null`. e2e `test_lua.py`: a fixture config loaded twice with `termo.reload()` fires its `@x` handler once |
| 3 | **Popup sizes and deferred `on_close`** (§3, first half). `src/lua/ui.c`: `ui_size` for `w`, `h`, `x`, `y` (number or `"N%"`), used by `api_popup` and `api_menu`; `popup_cb` and `menu_cb` schedule `on_close` with `event_once` and a client reference; `runtime/lua/termo/ui.lua` docs | Lua spec `ui_spec.lua`: `"80%"` accepted, `"80"` and `"x%"` rejected with an error. e2e `test_lua_ui.py`: `w = "50%"` on an 80-column `Pty` draws a 40-column popup; a popup opened from another popup's `on_close` is on screen after the first closes |
| 4 | **Callback context client** (§3, second half). `src/lua/api.c`: `api_context_set/get`, used by `api_client` and `api_eval` when there is no item; `src/lua/timer.c`: `struct lua_timer` and `struct lua_job` keep a referenced client captured at creation and set it around the call; `src/lua/ui.c`: the deferred `on_close` sets it | `timer_spec.lua`: `termo.eval("#{client_name}")` inside `termo.defer` equals the value outside. e2e `test_lua_ui.py`: with two clients attached, a `defer` created from a binding on the older client opens its popup on that client. `cmd_spec.lua` unchanged (the command context rule holds) |
| 5 | **`mode.lock` with `pass`** (§4). `runtime/lua/termo/mode.lua` | `mode_spec.lua`: `lock(k, { pass = { "C-a" } })` rebinds root `C-a` to `send-keys C-a` and `unlock` restores the previous binding. e2e `test_lua_ui.py`: locked, `C-a` reaches the pane (`cat -v` shows `^A`) |
| 6 | **Modes: groups, counts, sheet** (§5). `runtime/lua/termo/mode.lua`: `groups`, `enter`, `color`, `count`, digits, `#{mode_count}`, `mode.sheet`, `sheet_delay`, `?` | `mode_spec.lua`: the line of a grouped spec, counted string rhs repeated 3 times for `3`, expiry, `mode_count`. e2e `test_lua_ui.py`: `3` then `j` in a mode moves three panes; `?` opens the sheet and choosing a key runs it; with `sheet_delay = 200` the sheet appears without a key and does not when a key comes first |
| 7 | **Hideable floats** (§6). `src/core/termo.h` `LAYOUT_CELL_HIDDEN`; `src/window/window.c` `window_pane_is_visible` and the active-pane choice on hide; `src/cmd/pane/join.c` `-P hide|show`; `src/cmd/pane/select.c` shows a hidden target; `src/format/format.c` `pane_hidden_flag`, `window_hidden_floats`; `src/layout/custom.c` keeps the flag out of the layout string; `runtime/lua/termo/float.lua` `hide_all`, `show_all`, `toggle_all`; `docs/man/termo.1`; `docs/SYNCING.md` | unit `test_layout.c`: a hidden floating cell keeps `xoff`, `yoff`, `sx`, `sy` and is not visible; tiled cells ignore the flag. e2e `test_layout.py`: hide a float running `sleep 100`, `#{pane_pid}` unchanged and `window_hidden_floats` 1, not in `nest()`'s capture; show restores geometry; hiding the active float selects the last pane; `select-pane -t` a hidden float shows it. `float_spec.lua` for `toggle_all`'s three branches. `just upstream-regress` green |
| 8 | **`termo.pick`** (§7). `runtime/lua/termo/pick.lua`, `init.lua` | `pick_spec.lua`: without fzf (`PATH` without it) a list becomes a menu whose choice calls `on_choice("", line)`. e2e `test_lua_ui.py`: with fzf present (skipped otherwise) a `source` list is filtered and Enter returns the line |
| 9 | **Session picker** (§7). `runtime/lua/termo/sessionizer.lua` as `termo.session.picker`; `docs/sdlc/plan/006-rust.md` row 15 already points here | `session_spec.lua`: the collected list orders live, saved, projects and hides names in `opts.hidden`. e2e `test_session.py`: choosing a project directory creates a session with that cwd and switches the client |
| 10 | **Theme and health** (§7). `runtime/lua/termo/theme.lua`, `runtime/lua/termo/themes/{kanagawa,catppuccin,gruvbox}.lua`, `runtime/lua/termo/health.lua`, `colorscheme` and `health` user commands | `theme_spec.lua`: `apply` sets the ten style options; unknown name errors. `health_spec.lua`: the report has every section and a registered one; e2e `test_lua_ui.py`: `:health` opens a popup |
| 11 | **`#{git_branch}` and vim commands** (§7). `runtime/lua/termo/init.lua` (format), `command.lua` `vim()` | `format_spec.lua`: a tmp repo on branch `x` gives `x`, a detached HEAD gives 7 hex chars, outside a repo empty. `command_spec.lua`: `vim()` defines the six commands and the alias, twice without error |
| 12 | **`modal` preset** (§8). `runtime/pack/dist/opt/modal/{termo.json,lua/modal.lua,lua/modal/*.lua}`; `src/lua/api.c` `runtime_dir()`; `runtime/lua/termo/pack.lua` resolves a bare name in `load`; `docs/plugins.md` section; `docs/example_init.lua` loads it commented | new e2e module `tests/e2e/test_modal.py` (registered in `tests/meson.build`): `C-a` enters NORMAL, `s` splits and the mode stays, `Space` then `o` opens the picker, `Escape` returns to INSERT, `C-a` in LOCKED reaches the pane; `setup{ enter = "C-g", leader = "," }` moves both. `pack_spec.lua`: `load("modal")` resolves to the runtime dir |
| 13 | **Dotfiles** (outside the tree, `~/repos/dotfiles/termo`). Rewrite `lua/config/` over steps 1 to 12: `modal.setup` with `labels` in Spanish, the theme, `projects` roots, icons, the health tool list; delete `util.to_normal`, `mouse.lua`, `whichkey.lua`, `pick.lua`, `floats.lua`, `sessions.lua`, the `reset` machinery | `wc -l lua/config/*.lua` before (2,6xx) and after, recorded in this plan's Close; a day of use without a regression noted |

## Order and dependencies

1 first: 5, 6 and 12 build on sticky tables. 3 before 4 (the deferred `on_close` is where
the context is set). 2 anytime before 12. 7 is independent C and can land in parallel with
2 to 6. 8 before 9 and 10 (both use `termo.pick`); 10 before 12 (the preset's colours come
from `termo.theme`). 12 last in the tree; 13 after 12.

## Files

C: `src/server/client.c`, `src/config/options-table.c`, `src/config/cfg.c`,
`src/lua/{api,ui,timer}.c`, `src/core/termo.h`, `src/window/window.c`,
`src/cmd/pane/{join,select}.c`, `src/format/format.c`, `src/layout/custom.c`.
Lua: `runtime/lua/termo/{init,mode,palette,ui,float,pick,sessionizer,theme,health,command,pack}.lua`,
`runtime/lua/termo/themes/`, `runtime/pack/dist/opt/modal/`.
Tests: `tests/lua/*_spec.lua` (new: `pick`, `theme`, `health`, `float`), `tests/lua/run.lua`
list, `tests/unit/test_layout.c`, `tests/e2e/{test_lua_ui,test_lua,test_layout,test_session}.py`,
new `tests/e2e/test_modal.py`, `tests/meson.build`.
Docs: `docs/api.md` (generated), `docs/man/termo.1`, `docs/SYNCING.md`, `docs/plugins.md`,
`docs/example_init.lua`, `CLAUDE.md` (Lua runtime section).

## Risks

- **Dispatch divergence (step 1).** Upstream rewrote `server_client_key_callback` in 3.4, 3.5
  and 3.6. Mitigation: the change is two guarded branches behind a table lookup that is
  false for every table unless `sticky-key-tables` names it; `SYNCING.md` names the branches;
  the full upstream regress runs on the step.
- **Swallowing in sticky tables.** Today `Any` in a mode is Lua-visible and replaceable;
  after step 1 the swallow is in C. A config that relied on `Any` in a mode keeps working
  (a bound `Any` still wins), which step 1's e2e checks.
- **Deferred `on_close` (step 3).** Callers that read state right after the popup closes see
  it one loop iteration later. The dotfiles' `defer(30)` is the only known dependant.
- **Context client lifetime (step 4).** A captured client can detach before the callback; the
  reference keeps the struct, and `api_client` skips a client without a session, falling
  back as today.
- **Hidden floats (step 7).** A code path that walks panes without `window_pane_is_visible`
  could draw or select a hidden float. Mitigation: grep of `TAILQ_FOREACH.*z_index` and
  `window_get_active_at` reviewed in the step, and the e2e selection cases.
- **Menu as key sheet (step 6).** One column; a mode with many keys gives a tall menu. The
  dotfiles' multi-column sheet is not reproduced; open for a later step if it matters.

## Close

The item closes when steps 1 to 12 are landed with `meson test` and `just upstream-regress`
green, step 13's line counts are recorded here, and `ROADMAP.md` names item 007 as done
with the date.
