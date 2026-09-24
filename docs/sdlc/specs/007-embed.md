# Spec 007: termo as a host-neutral core, wasm32 as the first foreign host

- Status: Draft 2026-09-24
- Intent: [`intent/007-embed.md`](../intent/007-embed.md)
- Plan: [`plan/007-embed.md`](../plan/007-embed.md)

## Facts the design rests on

Read in the tree on 2026-09-24 unless a source is named.

- **Build.** `libtermo` (`meson.build:557-564`) archives every source but
  `src/core/main.c`; `termo`, `termo-test` and the fuzzers link it. libevent is a system
  dependency with no wrap (`:261`), ncurses is required (`:265-269`), `util` optional
  (`:272`). `luajit` is a feature option resolving `dependency('luajit')` (`:297`); found,
  it adds `src/lua/*.c` and `src/cmd/run-lua.c` and defines `HAVE_LUAJIT` (`:344`).
  `TMUX_CONF` and `TERMO_RUNTIME` are `c_args` defines (`:318-321`). The host switch is
  `:356-370`: five `host_os` branches each adding one `src/osdep/osdep-<os>.c`, else
  `error('unsupported host')`. `src/compat/fdforkpty.c` is always compiled (`:353`) and is
  the only caller of `forkpty`; `getptmfd` returns `INT_MAX` there. No cross file exists.
  `subprojects/` holds two Rust wraps (`unicode-width`, `unicode-segmentation`) with
  `packagefiles/<name>/meson.build`, sha256-pinned. WrapDB (fetched 2026-09-24) has `lua`
  5.3 to 5.5 and `utf8proc`, no `libevent`, no Lua 5.1.
- **Process model.** `main()` always ends in `client_main()` (`src/core/main.c:419`); the
  client connects (`src/client/client.c:122-126`) and on failure calls `server_start()`
  (`:164`). `server_start` (`src/server/server.c:184-283`) blocks signals, forks and
  daemonizes through `proc_fork_and_daemon` unless `CLIENT_NOFORK`, `event_reinit`s,
  `proc_start("server")`, sets signals, pledges, then builds the state (`input_key_build`,
  `utf8_update_width_cache`, `RB_INIT` of windows, panes, sessions, `TAILQ_INIT` of
  clients, `key_bindings_init`, `control_build_events`, `hooks_build_events`,
  `termo_lua_init`, message log, `start_time`), creates the socket, creates the forking
  client with `server_client_create(fd)`, arms the tidy timer, `server_acl_init`,
  `server_add_accept`, and runs `proc_loop(server_proc, server_loop)`; after the loop
  `job_kill_all`, `prompt_save_history`, `termo_lua_free`, `exit(0)`. `server_loop`
  (`:286`, static) drains the global queue and every identified client's queue until
  empty, calls `server_client_loop`, and returns 1 when the server should exit.
- **Client creation and identify.** `server_client_create(fd)` (`src/server/client.c:278`)
  sets the fd non-blocking, registers it as an imsg peer with `proc_add_peer`, creates
  the environ, queue, status, root key table and timers, and appends to `clients`. The
  server sends the client `MSG_EXIT`, `MSG_SUSPEND`, `MSG_LOCK`, `MSG_SHELL`,
  `MSG_STDOUT`/`MSG_STDERR` and the `MSG_READ_*`/`MSG_WRITE_*` file messages on that peer.
  Identify is `server_client_dispatch_identify` (`:2879-3030`): a `switch` over
  `MSG_IDENTIFY_*` filling `term_name`, `term_caps`/`term_ncaps` (a `char *[]` of
  `name=value`), `term_features`, `flags`, `fd` (`imsg_get_fd`, `:2951`), `out_fd`,
  `ttyname`, `cwd`, `pid`, `environ`; then, on `MSG_IDENTIFY_DONE` (`:2977`), the tail:
  `CLIENT_IDENTIFIED`, the client name, `control_start` or `tty_init` + `tty_resize` +
  `CLIENT_TERMINAL`, the `client-created` event, the paste time limit, and `start_cfg()`
  for the first client. `MSG_RESIZE` (`:2683`) is `tty_resize`, `tty_repeat_requests`,
  `recalculate_sizes`, overlay resize, `server_redraw_client`.
- **Terminfo.** `tty_term_read_list` (`src/tty/term.c:713-782`) is the only ncurses user
  in the tree (`setupterm`, `tigetstr`, `tigetnum`, `tigetflag`, `del_curterm`), called
  once by the client (`src/client/client.c:326`) guarded by `isatty` and a non-empty
  `TERM`; `tty_term_create` (`:547`) consumes the list. `terminal-overrides` and
  `terminal-features` apply on top (`tty_term_apply_overrides`, `:441`).
- **TTY.** `tty_init` (`src/tty/tty.c:105`) does `tcgetattr` (`:118`) and
  `ioctl(TIOCGWINSZ)` (`:130`) on `c->fd`; `tty_open` (`:275-307`) sets `EV_READ` and
  `EV_WRITE` events on it; `tty_add` (`:645`) is the single write funnel;
  `tty_start_tty` `tcsetattr`s (`:354`).
- **Panes and jobs.** `spawn_pane` (`src/core/spawn.c`) computes a `winsize` (`:449-453`),
  skips the fork for `SPAWN_EMPTY` (`:461`), then `new_wp->pid = fdforkpty(ptm_fd,
  &new_wp->fd, new_wp->tty, NULL, &ws)` (`:479`); `-1` is "fork failed", `0` is the child
  (the exec sequence, `:497-575`), anything else the parent, which goes on to
  `window_pane_set_event` (`src/window/window.c:1611`: `bufferevent_new` on `wp->fd`,
  `input_init`). Keys go out with `bufferevent_write` (`src/input/keys.c:418`); resize is
  `ioctl(TIOCSWINSZ)` and `fatal` on error (`src/window/window.c:606`); `FIONREAD` at
  `:491` tolerates error. `job_run` (`src/core/job.c:112-120`): `fdforkpty` when
  `JOB_PTY`, else `socketpair` + `fork`, `goto fail` when `socketpair` fails.
  `kill(pid, SIGCONT)` in `server.c:540` and `kill` in `job.c:280,417` tolerate errors.
- **Config.** `start_cfg` (`src/config/cfg.c:69`) queues `cfg_files[]` (set by
  `expand_paths(TMUX_CONF)` in `main.c:259`) and a `cfg_done` callback;
  `load_cfg_from_buffer` (`:206`) queues a buffer's commands; `.lua` files are
  dispatched at `:156`. `main.c` aborts without a UTF-8 locale (`:238-247`).
- **Lua.** `src/lua/runtime.c` includes `<luajit.h>` (`:13`) and calls `luaJIT_setmode`
  to switch the engine off (`:271`); everything else is the Lua 5.1 C API. The runtime
  dir is `TERMO_RUNTIME` or the env var (`:206-210`). `runtime/lua/termo/*.lua` uses
  `setfenv` and no `goto`, `ffi`, `bit`, `jit` or `table.new`: Lua 5.1 syntax.
- **Tests.** `tests/unit/harness.c`: `termo_test_init` creates `global_environ`, the
  three option trees from `options_table`, the width cache, `libevent` via
  `osdep_event_init`, `socket_path`, and returns early when `global_options` is set;
  `termo_test_drain` is `cmdq_next(nullptr)` until 0 plus `event_base_loop(libevent,
  EVLOOP_NONBLOCK)`. `tests/meson.build` lists `unit_sources` and `unit_modules`, adds
  `test_lua.c` only with LuaJIT, and registers `unit-<module>` as TAP tests. `src/lua/cmd.c`
  runs a command with captured output through `cmdq_new_state`, `cmdq_capture`,
  `cmdq_get_command`, `cmdq_append`, `cmdq_next(nullptr)` and a done callback
  (`lua_cmd_queue`, `api_cmd`).
- **Emscripten** (`src/lib/libsyscall.js`, `libfs.js`, `libtty.js` on `main`, read
  2026-09-24). `poll` computes readiness as `stream.stream_ops.poll?.(stream) ??
  (POLLIN|POLLOUT)`; there is no `select` syscall, `epoll` exists. `ioctl` `TCGETS`,
  `TCSETS`, `TIOCGWINSZ`, `TIOCSWINSZ` return `ENOTTY` unless `stream.tty` is set and
  then call `tty.ops.ioctl_tcgets`, `ioctl_tcsets`, `ioctl_tiocgwinsz`; `TIOCSWINSZ`
  returns 0 without a callback; `FIONREAD` goes to `FS.ioctl`. `TTY.register(dev, ops)`
  stores `{input, output, ops}` and `FS.registerDevice(dev, TTY.stream_ops)`, whose `open`
  sets `stream.tty`; `TTY.stream_ops.read` throws `EAGAIN` when `get_char` returns
  `undefined` with nothing read and treats `null` as EOF; `TTY.stream_ops` has no `poll`.
  `FS.mkdev(path, mode, dev)` creates the node. No `socketpair`, `forkpty` or `openpty`;
  `fork` fails at runtime; `pipe` exists. `-fsanitize=address,undefined` is supported.
  LuaJIT has no wasm backend.
- **Local machine.** Node 24, pnpm, bun; no emsdk. CI images from `ci/Dockerfile.*`
  built by `image.yml`, tagged by the Dockerfile's sha256.

## 1. The host contract (`src/host/host.h`)

```c
struct event_base *host_event_init(void);
pid_t	 host_pty_open(int ptmfd, int *master, char *name, struct winsize *ws);
char	*host_get_name(int fd, char *tty);
char	*host_get_cwd(int fd);
int	 host_term_caps(const char *term, int fd, char ***caps, u_int *ncaps,
	    char **cause);
```

`host_pty_open` has `fdforkpty`'s meaning: `-1` failed, `0` the child, else the parent
with `*master` open and `name` filled. A host without processes never returns `0`.
`host_term_caps` has `tty_term_read_list`'s meaning. The three `osdep_*` prototypes stay
in `termo.h` for this item (intent axis 1, open).

`src/host/host-posix.c`, compiled on the five POSIX targets: `host_event_init` calls
`osdep_event_init`; `host_pty_open` calls `fdforkpty`; `host_get_name` and `host_get_cwd`
call `osdep_*`; `host_term_caps` calls `tty_term_read_list`. Call sites:
`src/core/spawn.c:479`, `src/core/job.c:116` (`host_pty_open`),
`src/client/client.c:326` (`host_term_caps`), `src/core/main.c` and
`tests/unit/harness.c` (`host_event_init`), `src/core/names.c` and the two
`osdep_get_cwd` readers (`host_get_*`). Nothing else changes; `fdforkpty` and
`tty_term_read_list` keep their names and files.

## 2. The embedding API (`src/host/embed.c`, in `libtermo` on every target)

```c
int	 termo_embed_start(const char *term, int ttyfd, int peerfd, u_int sx, u_int sy,
	    const char *conf, char **cause);
int	 termo_embed_tick(void);
void	 termo_embed_resize(u_int sx, u_int sy);
const char *termo_embed_cmd(const char *cmd);
void	 termo_embed_stop(void);
```

- **`termo_embed_start`.** If `global_options` is `nullptr` (the binary's `main` has not
  run, which is every embedding), create what `main()` creates before `server_start`:
  `setlocale(LC_CTYPE, "C.UTF-8")` falling back to `""`, `global_environ` from `environ`,
  the option trees from `options_table` (the loop in `harness.c:options_defaults`, which
  moves to `src/config/options.c` as `options_create_defaults()` so `main.c`, the harness
  and the embed share it), `socket_path` set to `"embed"`, `libevent = host_event_init()`.
  Then `server_proc = proc_start("server")` (logging and the process struct, no signals),
  `server_init(0)` (§3), `c = server_client_create(peerfd)`, and the identify fields the
  imsg path would have filled: `c->fd = ttyfd`, `c->term_name = xstrdup(term)`,
  `host_term_caps(term, ttyfd, &c->term_caps, &c->term_ncaps, cause)` (failure returns
  `-1` with `cause`), `c->term_features` from `tty_get_features` of the caps as the
  client does, `c->flags |= CLIENT_UTF8`, `c->ttyname = xstrdup("embed")`, `c->cwd` from
  `find_cwd()`, `c->pid = getpid()`, `c->environ` copied from `global_environ`; then
  `server_client_identified(c)` (§3), which runs `tty_init` on `ttyfd`, sets
  `CLIENT_TERMINAL` and calls `start_cfg()` with `cfg_nfiles == 0`, so only `cfg_done`
  is queued; then `load_cfg_from_buffer(conf, strlen(conf), "embed", nullptr, nullptr,
  0, 0)` when `conf` is not `nullptr`. Size: `c->tty.sx/sy` are what `tty_init` read from
  the tty (`TIOCGWINSZ`), so the host's device or pty must already report `sx` by `sy`;
  the arguments are asserted against it, not applied (one source of truth, the tty).
  Returns 0. No session is created: the host's first `termo_embed_cmd("new-session ...")`
  attaches the client, exactly as a `termo` invocation would.
- **`termo_embed_tick`.** `current_time = time(nullptr)`; `int done = server_loop();`
  `event_base_loop(libevent, EVLOOP_NONBLOCK); return done;`. A host calls it from its
  loop (a JS interval, a GUI idle callback, the test after every write); libevent's
  timers fire when the tick runs, so the host's period bounds their latency (16 ms on the
  web host, comparable to a frame).
- **`termo_embed_resize`.** The `MSG_RESIZE` case body (`client.c:2683-2699`), extracted
  as `server_client_resized(struct client *)` and called by both; the host has already
  changed what `TIOCGWINSZ` reports.
- **`termo_embed_cmd`.** `cmd_parse_from_string`; on parse error return the error. Else
  `cmdq_new_state(&fs, nullptr, 0)` with `cmd_find_from_client(&fs, c, 0)`,
  `cmdq_capture(state, out, err)` over two static `evbuffer`s cleared on entry,
  `cmdq_append(c, cmdq_get_command(cmdlist, state))` followed by a done callback item
  that sets a flag, then `termo_embed_tick()` until the flag is set or the queue is
  waiting (a command that blocks on a job or a prompt: the function returns the empty
  string and the item completes on later ticks; the playground never issues one).
  Returns `err` when non-empty, else `out`, both `nul`-terminated in a static buffer valid
  until the next call. The shape is `src/lua/cmd.c:lua_cmd_queue` without the Lua ref.
- **`termo_embed_stop`.** `server_client_lost(c)` for the embedded client if it is still
  in `clients`, then the three calls `server_start` makes after `proc_loop`
  (`job_kill_all`, `prompt_save_history`, `termo_lua_free`). A host that wants the
  sessions gone runs `kill-server` first and sees `termo_embed_tick` return 1.

## 3. Core changes, no `#ifdef`, no observable change

- `src/server/server.c`: `server_init(uint64_t flags)` is the block from
  `server_client_flags = flags` through `gettimeofday(&start_time)` plus the tidy timer
  and `server_acl_init()`, moved out of `server_start` and called from it in the same
  place; `server_loop` loses `static` and gains a prototype in `termo.h`.
- `src/server/client.c`: the tail of `server_client_dispatch_identify` from
  `c->flags |= CLIENT_IDENTIFIED` to `return (0)` becomes
  `server_client_identified(struct client *)`, public; the `MSG_RESIZE` body becomes
  `server_client_resized(struct client *)`, public.
- `src/config/options.c`: `options_create_defaults()`, the loop from `harness.c`;
  `main.c` keeps its own loop for this item (it also applies `-f` and `TMUX_CONF`
  expansion around it) unless the diff is a pure replacement, which the plan step
  verifies.
- `src/core/spawn.c`, `src/core/job.c`, `src/client/client.c`, `src/core/main.c`,
  `src/core/names.c`: the renamed calls of §1.
- `src/core/termo.h`: the prototypes; `#include "host/host.h"` next to `compat.h`.

Recorded in `docs/SYNCING.md` as a divergence in files that are still tmux
(`server.c`, `client.c`, `spawn.c`, `job.c`): upstream cherry-picks into those functions
apply on top of the split.

## 4. The web host

### 4.1 Devices (`src/web/library.js`, an Emscripten `--js-library`)

One device kind, used for the client tty and every pane pty. `termo_web_device(N)`
registers major `dev = FS.makedev(64, N)` with `TTY.ttys[dev] = {input: [], output:
[], ops}` and `FS.registerDevice(dev, {...TTY.stream_ops, poll})`, then `FS.mkdev` at
`/dev/termo/<N>` (`0` is the tty, `1..` the ptys). `ops`: `get_char` shifts `input` or
returns `undefined` (so reads are `EAGAIN`, never EOF, until `close`), `put_char`
appends to a `Uint8Array` chunk delivered to `Module.termoHost.output(N, bytes)` on
`fsync` and at the end of each `write`, `ioctl_tcgets` returns the default termios,
`ioctl_tcsets` returns 0, `ioctl_tiocgwinsz` returns `[rows, cols]` from a size the host
sets. `poll` returns `POLLIN` when `input` is non-empty, always `POLLOUT`. `close`
calls `Module.termoHost.closed(N)`. `Module.termoHost.input(N, bytes)` pushes and the
page ticks. The device is the web's pty: the core sees an fd that `poll`s, reads,
writes, answers `TCGETS`, `TIOCGWINSZ` and swallows `TIOCSWINSZ`, which is what a pty
does for a process that never asks.

### 4.2 `src/host/host-web.c`

`host_event_init` returns `event_init()`. `host_pty_open(ptmfd, master, name, ws)`:
`n = ++next` (starting at 1), `termo_web_pty_open(n, ws->ws_col, ws->ws_row)` (a JS
import in `library.js` that creates the device and calls `Module.termoHost.spawn(n, cols,
rows)`), `*master = open("/dev/termo/<n>", O_RDWR | O_NONBLOCK)`, `name` set to that path,
returns `1`: the pid every web pane reports; `kill` and `waitpid` on it fail with
`ENOSYS`, which every site tolerates. `host_get_name` and `host_get_cwd` return
`nullptr`. `host_term_caps` returns the generated table (§4.3) as a freshly `xstrdup`ed
array so `tty_term_free_list` keeps working, `-1` with a cause for any other name.

The page (item 008) is the only caller of the API: it opens `/dev/termo/0` for the tty
and a second device for the peer, calls `termo_embed_start("xterm-256color", ttyfd,
peerfd, cols, rows, conf)`, then `new-session`, and ticks. Pane bytes flow between the
page's programs and the devices; the core does not know what answers.

### 4.3 Terminal capabilities without ncurses

`tools/gen-caps.c`, a native executable linked to `libtermo` (a Meson target, not
installed), calls `tty_term_read_list("xterm-256color", -1, ...)` and prints
`src/web/caps-xterm-256color.c`: `const char *const termo_web_caps[] = { "acsc=...",
... }` and `termo_web_ncaps`. The file is committed; a CI step regenerates it and
`diff`s, as `docs/api.md`. The name is fixed: the page always identifies as
`xterm-256color`, and xterm.js implements that.

### 4.4 Build

`ci/emscripten.ini`: `[binaries] c = 'emcc'`, `ar = 'emar'`, `strip = 'emstrip'`,
`[host_machine] system = 'emscripten'`, `cpu_family = 'wasm32'`, `cpu = 'wasm32'`,
`endian = 'little'`. `meson.build`:

- The libevent and ncurses lookups become conditional: on `emscripten`, `libevent_dep =
  dependency('libevent', fallback: ['libevent', 'libevent_dep'])` with a
  `subprojects/libevent.wrap` (2.1.12-stable tarball, sha256) and
  `packagefiles/libevent/meson.build` listing `event.c buffer.c bufferevent.c
  bufferevent_sock.c bufferevent_filter.c bufferevent_pair.c bufferevent_ratelim.c
  evmap.c evthread.c evutil.c evutil_rand.c evutil_time.c log.c poll.c signal.c
  strlcpy.c` with a checked-in `event-config.h` (`EVENT__HAVE_POLL`, no `select`, no
  `epoll`, no pthreads, no openssl) and `event2/event-config.h` layout; ncurses is not
  looked up and the branch adds `src/web/caps-xterm-256color.c`.
- `luajit_dep` on `emscripten` resolves `dependency('lua51', fallback: ['lua51',
  'lua51_dep'])`: `subprojects/lua51.wrap` (lua-5.1.5 from lua.org, sha256) and
  `packagefiles/lua51/meson.build` building `src/*.c` minus `lua.c`, `luac.c`, `print.c`
  as a static library with `-DLUA_USE_POSIX`; `HAVE_LUAJIT` is defined as today.
  `src/lua/runtime.c:13` and `:271` go under `#if __has_include(<luajit.h>)`.
- The host branch: `elif host_os == 'emscripten'` adds `src/host/host-web.c` and
  `src/web/caps-xterm-256color.c`, sets `TERMO_RUNTIME` to `/runtime`, `TMUX_CONF` to
  `''`, and skips `src/compat/fdforkpty.c` (its `forkpty` does not link; `getptmfd` moves
  to `host-posix.c`). The other branches add `src/host/host-posix.c`.
- The executable on `emscripten`: `executable('termo', 'src/web/main.c', link_whole:
  termo_lib, name_suffix: 'js', link_args: [...])` with `src/web/main.c` an `int
  main(void) { return 0; }` and the link args `-sMODULARIZE -sEXPORT_ES6
  -sEXPORT_NAME=createTermo -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH
  -sNO_EXIT_RUNTIME -sEXPORTED_FUNCTIONS=_termo_embed_start,_termo_embed_tick,
  _termo_embed_resize,_termo_embed_cmd,_termo_embed_stop,_malloc,_free
  -sEXPORTED_RUNTIME_METHODS=FS,TTY,cwrap,UTF8ToString,stringToNewUTF8
  --js-library src/web/library.js --embed-file runtime/lua@/runtime/lua`; `install:
  false`. `-Db_sanitize=address,undefined` adds `-fsanitize=...` to both compile and
  link as on any target.
- `just wasm`: `meson setup build-web --cross-file ci/emscripten.ini -Drust=disabled
  -Dsystemd=disabled -Dutf8proc=disabled -De2e=disabled -Dwerror=true` then `meson
  compile -C build-web`.

### 4.5 Smoke (`tests/web/smoke.mjs`, suite `web`)

Node imports `build-web/termo.js`, sets `Module.termoHost` with an in-memory tty
(collects output, answers `spawn` by echoing input back into the pane so `send-keys`
shows up in `capture-pane`), opens `/dev/termo/0` and a peer device, calls
`termo_embed_start`, `new-session -s t -x 80 -y 24`, ticks, and asserts: the tty output
contains a status line; `termo_embed_cmd("display -p '#{window_panes}'")` is `1`;
after pushing `\x02%` into the tty and ticking, `2`; `capture-pane -p` on the new pane
returns the echoed prompt; `run-lua 'return termo.version()'` returns the version
string; `detach` writes an imsg header with `MSG_EXIT` on the peer; `kill-server` makes
`termo_embed_tick` return 1; `termo_embed_stop` returns with the sanitized build
reporting nothing. Registered in `tests/meson.build` under `if host_os == 'emscripten'`
with `find_program('node')`, suite `web`, and the sanitized build is what CI runs.

## 5. Native test (`tests/unit/test_embed.c`, module `embed`)

`openpty` (from `<util.h>`/`<pty.h>`, the `util` dependency already linked) gives
`master`/`slave` at 80x24 via `TIOCSWINSZ`; `socketpair` gives the peer.
`termo_embed_start("xterm-256color", slave, peer[1], 80, 24, "set -g status-right ''",
&cause)` returns 0 or the test skips with the cause when terminfo is missing.
Cases: `starts_and_draws` (after `new-session -d -s t` and `attach`, ticks until the
master yields bytes containing `[t]`, the default status left); `split_from_keys`
(`write(master, "\x02%", 2)`, ticks, `termo_embed_cmd("display -p '#{window_panes}'")`
is `"2"`); `cmd_captures_output_and_error` (`list-panes -F '#{pane_id}'` yields two
lines; `bogus-command` yields the parse error); `resize_recalculates` (`TIOCSWINSZ` on
the master to 100x30, `termo_embed_resize(100, 30)`, `#{client_width}` is `100`);
`lua_runs` (with LuaJIT: `run-lua 'return 1+1'` is `2`); `detach_sends_exit` (`detach`,
tick, `recv` on `peer[0]` sees an imsg header with `MSG_EXIT`);
`stop_is_clean` (`kill-server`, tick returns 1, `termo_embed_stop`). One process with
the other modules: `termo_embed_start` sees `global_options` set by the harness and
skips process bring-up; `termo_embed_stop` leaves the trees as `termo_test_reset`
expects. Added to `unit_sources` and `unit_modules`.

## 6. CI

`ci/Dockerfile.web`: `ubuntu:24.04`, `emsdk` at a pinned release installed under
`/opt/emsdk` and activated in `ENV`, Node 24 from the emsdk, `pnpm` pinned (for item
008), `meson`, `ninja-build`, `bison`, `ccache`, `git`; the same `LABEL` block as the
other images. `image.yml`: a third output `web`. `ci.yml`: job `web`, `needs: image`,
container `ghcr.io/<repo>-ci-web:<tag>`, steps `configure` (`just wasm`'s setup with
`-Db_sanitize=address,undefined`), `build`, `web` (`meson test -C build-web --suite web
--print-errorlogs`), logs on failure; the `caps fresh` step (`ninja -C build gen-caps &&
diff`) runs on the `gcc-14` job, where a native build exists. `nightly.yml` `variants`
gains nothing (the wasm build has one configuration). `docs/ci.md` documents the job.
The `main` ruleset adds `web` to the required checks once green.

## 7. Documentation

`CLAUDE.md`: a section "Hosts and embedding" (the contract, the API, `just wasm`, the
rule that a host file is the only place a platform enters). `ROADMAP.md`: Phase 6
paragraph. `docs/SYNCING.md`: the split functions. `docs/embed.md`: the API with the
native and web call sequences, the device semantics, what fails on the web host.

## Verification

- `meson test -C build --print-errorlogs` green under ASan and UBSan with the `embed`
  module; `just upstream-regress` reports the same set as before the item; `just
  bench-compare <sha before>` within 10% on every scenario.
- `-Dluajit=disabled` and `-Drust=disabled` build and pass (nightly variants).
- `just wasm` builds warning-free with `-Dwerror`; `meson test -C build-web --suite web`
  passes on the sanitized build; the `.wasm` size of the non-sanitized build is written in
  the plan's Progress section.
- The `web` job is green on the gate; `ninja gen-caps` reproduces the committed table.
