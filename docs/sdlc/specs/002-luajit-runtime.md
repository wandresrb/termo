# Spec 002: embedded LuaJIT runtime and `termo.api`

- Status: Approved 2026-09-07
- Intent: [`intent/002-luajit-runtime.md`](../intent/002-luajit-runtime.md)
- Plan: [`plan/002-luajit.md`](../plan/002-luajit.md)
- Parent: [`specs/001-termo-architecture.md`](001-termo-architecture.md)

## Runtime (`src/lua/runtime.c`)

One `lua_State` owned by the server process, created in `server_start` immediately after
`hooks_build_events()` and closed in the server exit path. `luaL_openlibs`, then `package.path`
set to `~/.config/termo/lua/?.lua;~/.config/termo/lua/?/init.lua;
~/.local/share/termo/pack/*/lua/?.lua;<runtime>/lua/?.lua` where `<runtime>` is
`TERMO_RUNTIME` from the environment or the compiled `<datadir>/termo/runtime`.

```c
int  termo_lua_init(void);
void termo_lua_free(void);
int  termo_lua_call(lua_State *, int nargs, int nresults, u_int budget_ms);
int  termo_lua_load_file(const char *path, struct cmdq_item *);
int  termo_lua_eval(const char *code, struct cmdq_item *, char **result, char **error);
```

`termo_lua_call` is the only way C enters Lua. It installs a `LUA_MASKCOUNT` hook every
10 000 instructions that checks a `get_timer()` deadline of `budget_ms` and raises an error
when exceeded, runs `lua_pcall`, removes the hook, and routes an error: `cfg_add_cause()` while
`cfg_finished == 0`, otherwise `server_add_message()` and, if the call has a client,
`status_message_set()`. Budgets: 50 ms for format and event callbacks, 2000 ms for
`init.lua`, `run-lua` and key handlers. No entry point may bypass it.

Config integration (`src/config/cfg.c`): `start_cfg()` appends, after the `cfg_files` loop and
before `cfg_done`, a `cmdq_get_callback(cfg_lua_init, NULL)` that loads
`~/.config/termo/init.lua` when present. `load_cfg()` and `source-file` treat a `.lua`
extension as Lua. Errors appear in the same config-error view as `termo.conf` errors.

## `termo.api` (`src/lua/api.c`)

A static table `struct termo_api_fn { const char *name; lua_CFunction fn; const char
*signature; const char *doc; }` drives registration, `termo.api.list()` and documentation.
Handles are tmux id strings (`$1`, `@2`, `%3`), resolved with `session_find_by_id_str`,
`window_find_by_id_str`, `window_pane_find_by_id_str`; a stale handle raises a Lua error.

| Function | Core call | Notes |
|---|---|---|
| `version()` | `getversion()` | |
| `list()` | table | name, signature, doc |
| `eval(fmt[, target])` | `format_single(item, fmt, c, s, wl, wp)` | target resolved by `cmd_find_target` from a handle string; default is the current command's target or none |
| `list_sessions()` / `list_windows(s)` / `list_panes(w)` | RB trees, `TAILQ_FOREACH(&w->panes)` | arrays of `{id, name, index, active}` |
| `get_option(name[, target])` | `options_search`, `options_get_*` | typed return: number, boolean, string, array table |
| `set_option(name, value[, target])` | `options_from_string`, `options_push_changes` | value converted with `tostring`; array tables set element by element |
| `cmd(str)` | `cmd_parse_from_string`, `cmdq_new_state`, `cmdq_get_command`, `cmdq_insert_after` or `cmdq_append(NULL)` + `cmdq_next(NULL)` | see context rule |
| `cmd_async(str, fn)` | as above plus `cmdq_get_callback` after the group | `fn(output, err)` |
| `on(event, fn)` / `off(id)` | `events_add_sink` once per name | returns an integer id |
| `emit(name, tbl)` | `event_payload_create`, `event_payload_set_*`, `events_fire` | `name` must satisfy `hooks_valid_event_name` (`@` prefix) |
| `keymap_set(table, key, rhs, opts)` / `keymap_del(table, key)` | `key_string_lookup_string`, `key_bindings_add`, `key_bindings_remove` | `rhs` string parses as commands; `rhs` function binds `run-lua -r <ref>` |
| `defer(ms, fn)` / `timer(ms, fn)` | `evtimer_set`, `evtimer_add` | `timer` returns a handle with `stop()` |
| `system(argv, opts)` | `job_run` with update, complete and free callbacks | `on_stdout(line)`, `on_exit(status)` |
| `menu(spec)` | `menu_create`, `menu_add_item`, `menu_display` with `menu_choice_cb` | items `{name, key, fn or cmd}`; `nil` item is a separator |
| `popup(spec)` | `popup_display` with `popup_close_cb` | `{cmd, w, h, title, on_close}` |
| `message(str)` | `status_message_set` | needs a client |
| `prompt(label, fn)` | `status_prompt_set` | needs a client |
| `format_add(name, fn)` | table read by `format_create` | see below |

**Command context rule.** `cmd()` called while a `cmdq_item` is running (a binding, a hook, a
`run-lua` command) inserts after that item with `cmdq_insert_after` and returns `nil`; output
is not available. Called with no running item (timer, event sink, `init.lua`'s own callback
context excepted as above), it appends to the global queue, drains with `cmdq_next(NULL)` and
returns `output, err`. If the item is still `CMDQ_WAITING` after draining, `cmd()` returns
`nil, "asynchronous command, use cmd_async"`.

**Capture.** `struct cmdq_state` gains `struct evbuffer *capture_out, *capture_err`.
`cmdq_print_data()` and `cmdq_error()` append to those when set instead of going to the
client. This is the only change to `src/cmd/queue.c` besides putting `command-error` on the
event bus.

**Events.** `on()` keeps a Lua table `name -> {id -> fn}` in the registry and one C sink per
name. The sink converts the payload with `event_payload_first/next`: strings, numbers, times as
numbers, and client/session/window/pane items as handle strings (client by name). Callbacks run
under `termo_lua_call`; one failing callback does not stop the rest. The events dispatched only
through `hooks_run` today (`command-error`, `pane-command-started`, `pane-command-finished`,
`pane-died`, `pane-exited`, `pane-mode-changed`, `pane-mode-entered`, `pane-mode-exited`,
`pane-prompt-opened`, `pane-prompt-closed`, `session-added-to-group`,
`session-removed-from-group`) are also fired with `events_fire*` at their existing sites.

**Key handlers.** `run-lua -r <ref>` looks the ref up in the registry and calls it with
`{client = name, key = key_string_lookup_key(...), table = tablename, mouse = {x, y, b}}` taken
from `cmdq_get_event(item)`. `keymap_del` removes the binding and unrefs.

**Format variables.** `format_add()` stores `name -> fn` in a registry table. `format_create()`
gains one call, `termo_lua_format_register(ft)`, that iterates that table and calls
`format_add_cb(ft, name, termo_lua_format_cb)`; the callback resolves `name` from the format
tree entry and returns `xstrdup` of the Lua result. Evaluation is lazy by the engine's design,
so an unreferenced variable costs nothing. No format modifier is added.

## `run-lua` (`src/cmd/run-lua.c`)

```
run-lua [-j] [-f path | -r ref | code]
```

`-f` loads a file, `-r` is the internal key-handler form, otherwise `code` is a chunk. The
chunk's return value is printed with `cmdq_print`; with `-j` it is encoded with
`runtime/lua/termo/json.lua`. Works from the CLI, from `bind-key`, and from control mode, so
any language reaches `termo.api` as `termo run-lua -j 'return termo.api.list_panes()'`.

## Runtime Lua (`runtime/lua/termo/`)

`init.lua` requires the rest and exposes `termo.keymap`, `termo.opt` (metatable proxy over
`get_option`/`set_option` with type conversion), `termo.ui`, `termo.on/off/emit`, `termo.defer`,
`termo.timer`, `termo.system`, `termo.format`, `termo.json`. Installed with `install_subdir`
to `<datadir>/termo/runtime`; found at build time through `TERMO_RUNTIME`, which
`meson devenv` exports as the source `runtime/`. Files under `~/.local/share/termo/pack/` load
in their own `setfenv` environment inheriting a read-only `_G`.

## Build

`meson.build`: `lua_sources` added when `luajit_dep.found()`, `-DTERMO_RUNTIME="<datadir>/
termo/runtime"`, `install_subdir('runtime')`. All Lua code is under `#ifdef HAVE_LUAJIT`;
`-Dluajit=disabled` must build and pass every suite (nightly variant). `tests/meson.build`:
`test_lua` in `unit_sources`, suite `lua` running `tests/lua/run.lua` through `run-lua -f`
against `termo -L test -f /dev/null`.

## Verification

- `tests/unit/test_lua.c`: init and close under ASAN; `while true do end` is cut by the budget
  with an error, not a hang; a runtime error is routed and the process lives; `eval` of a format
  with a session handle.
- `tests/lua/*_spec.lua`: one spec per `termo.api` function; the runner fails when a function
  in `termo.api.list()` has no spec.
- Regress unchanged with and without LuaJIT.
- ASAN clean from `new-session` to `kill-server` with the reference `init.lua` loaded.
