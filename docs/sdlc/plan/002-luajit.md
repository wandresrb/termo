# Plan 002: LuaJIT runtime and `termo.api`

- Status: Approved 2026-09-07 (starts after plan 004, C23 and POSIX, closes)
- Intent: [`intent/002-luajit-runtime.md`](../intent/002-luajit-runtime.md)
- Spec: [`specs/002-luajit-runtime.md`](../specs/002-luajit-runtime.md)

## Why an embedded language, and why Lua

Extension code in a multiplexer runs inside the server's single libevent thread, at these
points and with these budgets:

| Entry point | Frequency | Budget |
|---|---|---|
| status line (`#{...}`) | every `status-interval`, per client, and on every redraw | < 1 ms |
| key handler | every keystroke with a binding | < 1 ms |
| hook / event | tens per second with many panes | < 100 µs |
| config load | server start | < 50 ms |
| plugin UI | interactive | < 16 ms per frame |

Anything slower freezes every attached client. That gives six requirements, in order of
weight: in-process (tmux's `run-shell` is fork+exec, 2 to 5 ms, the reason TPM plugins
stutter); small runtime and instant startup; a C embedding API with no threads or loop of its
own; fault isolation (a broken config must not kill or hang the server); editable without a
compiler; and known to the target user.

Against those: **Rust** is a systems language and that is where termo uses it (phase 5). As a
plugin language its two routes both fail: native `.so` plugins have no stable ABI and a panic
kills the server; WASM is what Zellij did to avoid that and what termo rules out (multi-MB
binaries, serialised IPC per call, a toolchain to add a keybinding). **Go** cannot be embedded
sanely (its runtime owns the process, cgo crossings cost microseconds, +10 MB) and is not
scripting either. **Swift** is not portable to Linux/BSD embedding. **Kotlin** is the JVM.
**JavaScript**: QuickJS is embeddable but 10 to 50 times slower than LuaJIT with no JIT; V8 and
Deno are 20+ MB with their own event loop. **Python**: 30 MB, GIL, one global interpreter,
system version hell. **Guile/Janet**: no user base. **Data-only config** (TOML, KDL) works when
config is state; multiplexer config is behaviour, and `tmux.conf` itself is the proof, a command
language with `if-shell`, conditionals and arithmetic that grew by accident. Neovim went
through the same with VimL and replaced it with a real language, not a format.

**Lua/LuaJIT** meets all six: ~300 KB (LuaJIT ~500 KB), microsecond startup, a C API designed
for embedding (explicit state, no globals, stack based), `lua_sethook` instruction counting to
cut a hung script, `pcall` for isolation, coroutines over a C loop without threads, and with
LuaJIT near-C speed plus FFI. It is the de-facto embedded language of this niche: Neovim,
WezTerm, Hammerspoon, AwesomeWM, mpv, Redis, OpenResty, HAProxy, Wireshark. Known costs:
1-based indexing, globals by default, small stdlib, 5.1 syntax, one main maintainer; PUC Lua
5.1 stays API-compatible as a fallback because the core does not use FFI.

Neovim chose it in 2014-2017 for footprint, speed and C API simplicity over Python, JS and
Scheme. What termo copies is the second-order effect: `vim.api`, one table of flat C functions
exposed to Lua in-process and to any language out-of-process.

## Architecture

```
 user / plugins      init.lua, ~/.config/termo/lua/, ~/.local/share/termo/pack/*/lua/
 runtime Lua         runtime/lua/termo/{init,keymap,opt,ui,json,layout,palette,hints,float,pack}.lua
 termo.api (C)       src/lua/api.c: ~20 flat functions plus a metadata table
 runtime             src/lua/runtime.c: one lua_State, execution budget, error routing, init.lua
 tmux core           cmdq, events, key_bindings, options, format, menu, popup, job, evtimer
```

Design decisions, each bound to an existing core surface (see the spec for signatures):

- **Handles are tmux ids** (`$1`, `@2`, `%3`) as strings, resolved with `*_find_by_id_str`.
  Lua never holds C pointers.
- **Reading state is evaluating formats.** `termo.api.eval(fmt, target)` is `format_single`;
  the ~500 `#{}` variables become the introspection API for free. `list_sessions/windows/panes`
  iterate the RB trees and return ids plus a few fields.
- **Mutation is commands.** `termo.api.cmd(str)` parses, queues and drains. The one core change
  is a capture sink in `cmdq_print_data` and `cmdq_error` hung off `cmdq_state`, because output
  of a client-less command is discarded today. Context rule, the same one `hooks_insert_one`
  follows: inside a running item (binding, hook, `run-lua`, `init.lua`), `cmd()` inserts after
  it and returns no output; inside an event sink, which runs on the stack of the core operation
  that fired the event, it appends to the global queue and returns no output, never draining;
  from a timer or a job callback it drains `cmdq_next(NULL)` and returns `output, err`. A
  command that ends in `CMD_RETURN_WAIT` returns an "asynchronous" error; `cmd_async(str, fn)`
  chains a `cmdq_get_callback` and works in all three contexts.
- **Events are one sink per name** on the existing bus: `termo.on` registers
  `events_add_sink(name, lua_sink_cb, NULL)` next to `hooks_event_cb`; payloads become tables
  via `event_payload_first/next`; `termo.emit("@x", tbl)` uses the already-valid `@` names. Core
  change: the ten events that only pass through `hooks_run` today also go through
  `events_fire*`, so there is one bus.
- **Keymaps with functions**: `key_bindings_add` with the cmdlist `run-lua -r <ref>`; the
  `run-lua` entry reads the key event with `cmdq_get_event`. No dispatcher changes.
- **Timers, jobs**: `evtimer_*` on the server base; `job_run` with its three callbacks.
- **UI**: `menu_display` with `menu_choice_cb`, `popup_display` with `popup_close_cb`,
  `status_message_set`, `status_prompt_set`. All require a client, the current command's or an
  explicit one.
- **Status line from Lua without touching the format parser**: `termo.format.add(name, fn)`
  registers a variable that `format_create` adds with `format_add_cb`; evaluation is lazy. No
  `#{lua:}` modifier, `format.c`'s parser stays upstream.
- **Budget and isolation**: every C to Lua entry goes through `termo_lua_call(L, nargs, nres,
  budget, item)`: `LUA_MASKCOUNT` hook with a deadline (50 ms for format and event callbacks,
  2 s for `init.lua` and `run-lua`), `pcall`, errors to `cfg_add_cause` before `cfg_finished`,
  to `cmdq_error` when there is an item, else `server_add_message`. The budget is tmux's
  invariant that nothing blocks the loop, applied to the first user code that runs inside it;
  a stuck server cannot even be stopped with SIGTERM because signals arrive through libevent.
  The LuaJIT trace compiler is off by design: a compiled trace never calls a count hook and
  trace stitching compiles across C calls, so with it on nothing is interruptible; LuaJIT is
  here for the 5.1 dialect and its interpreter, and API calls dominate termo's Lua anyway
  (measured in the spec). The state also removes `os.exit`, `os.execute`, `io.popen`,
  `jit.on` and `debug.sethook`: a C call is not interruptible by the hook and the budget must
  have no off switch from Lua.
- **`run-lua [-j] [-f file | code]`** from the CLI, `bind-key`, or `termo -C`; `-j` prints JSON.
  That is the out-of-process path for Rust, Go, Python or an agent. JSON framing of control mode
  itself waits for a concrete case.
- **`init.lua`** is the last entry of the `TMUX_CONF` search list, so `-f` replaces it and
  tests with `-f /dev/null` never load it; `load_cfg()` queues a `.lua` file as a callback item
  (after the calling item, or appended), which covers `-f x.lua` and `source-file x.lua`. The
  state is created in `server_start` right after `hooks_build_events()`. Paths:
  `~/.config/termo/init.lua`, `~/.config/termo/lua/?.lua`, `~/.local/share/termo/pack/*/lua/?.lua`,
  `<datadir>/termo/runtime/lua/?.lua`, overridden by `TERMO_RUNTIME` (exported by `meson devenv`).
- **Metadata**: `api.c` defines `{name, fn, signature, doc}`; `termo.api.list()` returns it;
  `tools/gen-api-doc.lua` writes `docs/api.md`.
- Everything under `HAVE_LUAJIT`; `-Dluajit=disabled` builds and passes (nightly variant).

## Deliverables, one step each, green before the next

1. **Runtime.** `src/lua/runtime.{c,h}`, `src/cmd/run-lua.c` in `cmd_table`, `init.lua` as
   the last `TMUX_CONF` entry, `termo.api.version/eval/get_option/set_option/list`,
   `meson.build` (`lua_sources` under `luajit_dep.found()`, `TERMO_RUNTIME`,
   `install_subdir('runtime')`), `tests/unit/test_lua.c` (init/close under ASAN, `while true`
   and a loop calling the API cut by the budget, an error does not kill the process, `eval` of
   a format, the five removed functions are `nil` and `jit.status()` is false),
   `tests/lua/run.lua` plus first spec, suite `lua`.
2. **Commands.** Capture sink in `queue.c`, `termo.api.cmd` and `cmd_async` with the context
   rule. Spec: `cmd("display -p x")` returns `x`; `cmd("display-popup")` errors as asynchronous;
   from a binding `cmd` queues without output.
3. **Events.** Lua sink, `on/off/emit`, payload to table, the ten `hooks_run` events on the
   bus. Spec: each of the 39 events fires once with the expected payload; a throwing callback
   does not stop the others; `@custom` round-trips; `cmd("kill-window")` from a
   `window-layout-changed` sink runs after the `join-pane` that fired it, under ASAN.
4. **Keymaps, timers, processes.** `run-lua -r`, `termo.keymap.set/del`, `defer/timer`,
   `system`. Spec: a key runs the function with `{client, key, table}`; a timer cancels;
   `system{"printf","x"}` delivers `x` to `on_stdout`.
5. **Formats.** `termo.format.add`, hook in `format_create`. Spec: `status-right "#{lua_x}"`
   renders; the function is not called when nothing references it.
6. **UI.** `menu`, `popup`, `message`, `prompt`, with `capture-pane` snapshots.
7. **Runtime Lua.** `init`, `keymap`, `opt`, `ui`, `json`; then `palette` (menu over
   `src/core/fuzzy.c`), `hints` (key tables + `status-format[1]`), `layout`, `float` (bindings
   over `layout_floating_*`), last `pack` (`termo.json`, git under `~/.local/share/termo/pack/`).
8. **Docs.** `tools/gen-api-doc.lua` to `docs/api.md`; `docs/man/termo.1` gains `run-lua` and
   `init.lua`; README gets a 20-line `init.lua` with a keymap, a hook and a status variable.

## Files

Create: `src/lua/runtime.c`, `src/lua/runtime.h`, `src/lua/api.c`, `src/lua/api.h`,
`src/cmd/run-lua.c`, `runtime/lua/termo/*.lua`, `tests/unit/test_lua.c`, `tests/lua/run.lua`,
`tests/lua/*_spec.lua`, `tools/gen-api-doc.lua`, `docs/api.md`.

Modify: `meson.build`, `tests/meson.build`, `src/cmd/cmd.c`, `src/cmd/queue.c` (capture and
`command-error` on the bus), `src/core/termo.h`, `src/config/cfg.c`, `src/server/server.c`,
`src/format/format.c` (one call in `format_create`), `src/core/session.c`,
`src/window/window.c`, `src/core/spawn.c` (events on the bus), `docs/man/termo.1`, `README.md`,
`CLAUDE.md`.

Reuse untouched: `events_add_sink`/`events_fire*`, `event_payload_*`,
`key_bindings_add/remove/dispatch`, `cmd_parse_from_string`, `cmdq_new_state/get_command/
append/insert_after/next/get_callback`, `options_search/from_string/get_*/push_changes`,
`format_single/format_create/format_add_cb`, `menu_create/add_item/display`, `popup_display`,
`status_message_set/status_prompt_set`, `job_run/job_get_event`, `evtimer_*`,
`*_find_by_id_str`, `hooks_valid_event_name`.

## Risks

- **Re-entering the core** from Lua: inside a running item or an event sink, `cmd()` only
  queues, by construction; Step 2's spec exercises it from a binding and from a hook, Step 3's
  from a `window-layout-changed` sink calling `kill-window` during `join-pane`.
- **Lua refs tied to C objects** (timers, menus, popups, jobs): registry refs released in the
  close or free callback; ASAN through `lua_close` on `kill-server` is the test.
- **LuaJIT and ASAN**: LuaJIT has its own allocator; Step 1 decides between
  `LUAJIT_USE_SYSMALLOC` for the test build or accepting C-side coverage only.
- **Interpreter speed in format callbacks**: Step 5 measures a status line with Lua variables
  at `status-interval 1` and on every key. The compiler does not come back on for it; a slow
  callback is capped by its 50 ms budget and the fix is less work per redraw.
- **One `lua_State`**: files under `pack/` load in their own `setfenv` environment over a
  read-only `_G`; the user's `init.lua` is unrestricted beyond the five removed functions.

## Verification

- `meson test` green on the three CI cells with LuaJIT, and the nightly `-Dluajit=disabled`
  variant green, on every step.
- The `lua` suite covers every function in `termo.api.list()`; the runner fails if a listed
  function has no spec.
- A reference `init.lua` in `docs/` (function keymap, hook, Lua status variable, menu, popup)
  runs as an integration smoke test, ASAN clean from `new-session` to `kill-server`.
