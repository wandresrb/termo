# Spec 002: embedded LuaJIT runtime and `termo.api`

- Status: Approved 2026-09-07; amended 2026-09-07: compiler off, command context rule for
  sinks, removed stdlib
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
void termo_lua_init(void);
void termo_lua_free(void);
lua_State *termo_lua_state(void);
struct cmdq_item *termo_lua_item(void);
int  termo_lua_call(lua_State *, int nargs, int nresults, u_int budget_ms, struct cmdq_item *);
int  termo_lua_load_file(const char *path, struct cmdq_item *);
int  termo_lua_eval(const char *code, struct cmdq_item *, char **result);
```

`termo_lua_call` is the only way C enters Lua. It installs a `LUA_MASKCOUNT` hook every
10 000 instructions that checks a `get_timer()` deadline of `budget_ms` and raises an error
when exceeded, runs `lua_pcall` with a traceback handler, removes the hook, and routes an
error: `cfg_add_cause()` while `cfg_finished == 0`, `cmdq_error()` when the call has an item,
otherwise `server_add_message()`. Budgets: 50 ms for format and event callbacks, 2000 ms for
`init.lua`, `run-lua` and key handlers. No entry point may bypass it.

**Why the budget exists.** The server is one thread and one libevent loop, and nothing
user-supplied runs inside it: a command that must wait returns `CMD_RETURN_WAIT`, shell code
runs as a job, a hook queues its command list. Lua is the first user code inside the loop, and
the budget is that invariant made enforceable. It matters more than in Neovim because SIGINT,
SIGHUP and SIGTERM reach the server through libevent (`proc_set_signals`): a server stuck in
Lua ignores them, blocks every client, and only SIGKILL ends it, taking every session with it.

**Why LuaJIT, and why its compiler is off.** LuaJIT is chosen for the Lua 5.1 dialect shared
with Neovim plugins and for its interpreter, the fastest Lua has; FFI stays available. The
trace compiler is not part of the rationale and the state runs with it off,
`luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)`: a compiled trace never calls a
count hook, hot counting stays on while a hook is installed (`lj_dispatch_update`), and 2.1's
trace stitching compiles across C calls (`recff_c` is `recff_nyi`), so with the engine on no
loop is interruptible, not even one calling `termo.api` on every iteration (verified: the
server hung and ignored SIGTERM). Measured in the server: 5M iterations of pure arithmetic
19 ms interpreted, 5 ms compiled; 100k `get_option` 134 ms either way; 20k `eval` 232 ms
against 236 ms. termo's Lua is API calls plus glue and the API call dominates. Re-measured in
step 5 with real format callbacks, the only hot path.

**What the state removes.** The hook interrupts Lua bytecode, not a C function in progress,
and the guarantee must not have an off switch in Lua. After `luaL_openlibs` the runtime sets
to `nil`: `os.exit` (ends the server), `os.execute` and `io.popen` (fork and wait inside the
loop), `jit.on` (turns the engine back on), `debug.sethook` (replaces the budget hook).
Processes go through `termo.api.system`, which is a `job_run`. Every `termo.api` function
returns without waiting on anything; one that needs to wait takes a callback.

Config integration (`src/config/cfg.c`): `~/.config/termo/init.lua` is the last entry of the
`TMUX_CONF` search list, so `-f` replaces it like every other default file and tests with
`-f /dev/null` never load it. `load_cfg()` recognises a `.lua` extension and queues a
`cfg_lua_file` callback (inserted after the calling item, or appended to the global queue), so
the file runs after the commands loaded before it; that covers `-f x.lua` and
`source-file x.lua`. Errors appear in the same config-error view as `termo.conf` errors.

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

**Command context rule.** `cmd()` never runs a command while the core is in the middle of an
operation; it does what `hooks_insert_one` does. Three contexts:

- A `cmdq_item` is running (a binding, a hook, `run-lua`, and `init.lua` or `source-file
  x.lua`, which run as a callback item): `cmd()` inserts after that item with
  `cmdq_insert_after` and returns `nil`; output is not available.
- An event sink is running: `events_fire` calls sinks in place, inside whatever fired the
  event (`window-layout-changed` from `join-pane`, `window-renamed` from `window_set_name`),
  so the callback is on the stack of a half-finished mutation. `cmd()` appends to the global
  queue with `cmdq_append(NULL)` and returns `nil`; nothing runs until the core is back in the
  loop. The runtime knows it is in a sink through a counter the sink dispatcher raises around
  `termo_lua_call`. `emit()` from a sink is fine, the bus is reentrant.
- Neither (a timer, `defer`, a `system` callback): `cmd()` appends to the global queue, drains
  with `cmdq_next(NULL)` and returns `output, err`. If the item is still `CMDQ_WAITING` after
  draining, `cmd()` returns `nil, "asynchronous command, use cmd_async"`.

`cmd_async(str, fn)` works in all three: the callback item goes after the command group and
`fn(output, err)` runs when the queue reaches it.

**Capture.** `struct cmdq_state` gains `struct evbuffer *capture_out, *capture_err`.
`cmdq_print_data()` and `cmdq_error()` append to those when set instead of going to the
client. This is the only change to `src/cmd/queue.c` besides putting `command-error` on the
event bus.

**Events.** `on()` keeps a Lua table `name -> {id -> fn}` in the registry and one C sink per
name. The sink converts the payload with `event_payload_first/next`: strings, numbers, times as
numbers, and client/session/window/pane items as handle strings (client by name). Callbacks run
under `termo_lua_call` with the sink counter raised (see the command context rule); one failing
callback does not stop the rest. The events dispatched only
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
  with an error, not a hang; a loop calling `termo.api` on every iteration is cut the same way;
  a runtime error is routed and the process lives; `eval` of a format with a session handle;
  `os.exit`, `os.execute`, `io.popen`, `jit.on` and `debug.sethook` are `nil` and
  `jit.status()` is false.
- A sink for `window-layout-changed` that calls `cmd('kill-window')` sees the command run after
  `join-pane` returns, under ASAN.
- `tests/lua/*_spec.lua`: one spec per `termo.api` function; the runner fails when a function
  in `termo.api.list()` has no spec.
- Regress unchanged with and without LuaJIT.
- ASAN clean from `new-session` to `kill-server` with the reference `init.lua` loaded.
