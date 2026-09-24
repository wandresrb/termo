# Intent 007: termo as a host-neutral core, wasm32 as the first foreign host

- Status: Draft 2026-09-24
- Author: wandresrb

## Problem

termo runs in exactly one shape: a binary that is its own client, forks its own server,
talks to it over a Unix socket, and gives every pane a process through `forkpty`. That
shape is right for a terminal, and it is the only one the tree knows: `main.c` decides
client or server, `server_start` forks and binds, `spawn_pane` and `job_run` call
`fdforkpty` directly, the client reads terminfo through ncurses and ships the capability
list over imsg. Nothing in the core says which of those are the multiplexer and which are
the host it happens to run on.

Two things want termo in another host. The website (item 008) wants a real termo in the
page, rendered by xterm.js, to teach termo with lessons that ask termo itself whether the
goal was reached; a browser has no fork, no Unix socket, no pty and no ncurses. A desktop
application later wants termo in process with its own window instead of a terminal. Both
are the same question: where does termo end and the host begin. Answering it with a
second `main` full of `#ifdef __EMSCRIPTEN__` would leave the seam in one person's head
and untested on every commit; roadmap rule 1 wants every build in CI green under ASan and
UBSan, and a build that only exists in a browser cannot be.

Rule 5 of the roadmap is not touched by this: it forbids a WebAssembly runtime for
plugins inside termo. termo compiled to wasm32 is a build target of the tree, an artifact
of the website; the binary a user installs does not change.

## Outcome

The core states what it needs from a host in one header and gets it from one file per
host. The embedding API is a handful of functions compiled into every build, so a native
unit test drives termo in process, with a real pty and under ASan, on every commit; the
wasm32 build differs from it only by the host file and the link line. The website's
playground is the first consumer; a desktop host is a second host file, not a redesign.

## Axes

### 1. A host contract, and the POSIX host as its first implementation

Enters: `src/host/host.h`, the functions a host gives termo: an event base, a pty with a
size and a name, the name and cwd of the process behind a pty, the capability list of a
terminal name. `src/host/host-posix.c` implements them with what exists (`osdep_*`,
`fdforkpty`, `tty_term_read_list`), so the native binary changes nothing observable:
`just upstream-regress` and `just bench-compare` say so.
Does not enter: abstracting the exec of the child (the code between `fork` and `execvp`
in `spawn_pane` and `job_run` stays where it is; a host that has no processes returns
"parent" with a fixed pid), a plugin ABI for hosts, dynamic loading of hosts.
Open: whether `osdep_*` keep their names or fold into `host_*` once a second host exists.

### 2. termo in process: the embedding API

Enters: `src/host/embed.c`, compiled into `libtermo` on every target: start (bring up the
server state that `server_start` builds after its fork and socket, create the one client
over a tty fd and a peer fd the host provides, identify it, feed a configuration
string), tick (one `server_loop` iteration plus pending libevent callbacks, returning
whether the server wants to exit), resize, a command with captured output, stop (the
teardown `server_start` runs after `proc_loop`). Keys and screen bytes go through the tty
fd, not through the API: one I/O face, the same as the native client.
Does not enter: a second client, control mode in the embedded client, an embedded server
that also accepts socket clients, a C ABI promise beyond the tree (`libtermo` stays an
internal archive; a public library is intent 006's "out for now" row and stays there).
Decided: the client's imsg peer is part of the contract (the server sends `MSG_EXIT`,
`MSG_SUSPEND`, `MSG_LOCK` and the file messages to it); a native host passes a
`socketpair` end, a browser host a device that swallows.

### 3. wasm32 through Emscripten, as a Meson host

Enters: a cross file, an `emscripten` branch in `meson.build` next to the five targets,
`src/host/host-web.c` over Emscripten devices registered from `src/web/library.js`
(the JS half of the host: a tty device and one pty device per pane, with `poll` so
libevent's `poll` backend sees readiness, and the TTY ops so `tcgetattr` and
`TIOCGWINSZ` succeed without touching `tty.c`), a generated static capability table for
`xterm-256color` instead of ncurses, libevent and PUC Lua 5.1 as sha256-pinned
subprojects used only on this host, a Node smoke test as the `web` suite, a builder image
and a gate job. Panes on this host are whatever the page connects to the pty device; the
core does not know.
Does not enter: a spike outside the tree; wasi-sdk (no pty, no `poll` on devices, no
terminal ioctls); running the native client code in the page; Rust on this host until
plan 006 removes a C twin (then `wasm32-unknown-emscripten` joins, and plan 006 carries
that constraint); jobs without a pty (`socketpair` fails with `ENOSYS`, so `#()`
formats stay empty and `run-shell`, `if-shell`, `pipe-pane` fail cleanly on this host).
Decided: PUC Lua 5.1.5 rather than no Lua, because the playground exists to show
`init.lua`, the palette and floats; `src/lua/` touches LuaJIT in two lines and
`runtime/lua/` is 5.1 syntax; the wall-time budget holds because there is no JIT.

## Out of scope for the whole item

The website itself (item 008); a desktop host; a real shell in wasm; Web Workers as pane
processes (the page's concern, item 008 keeps the seam); `termo-web`, the WebSocket
bridge over control mode of spec 006 §9, which is a real server behind a browser and
not this.

## Evals

The native binary is unchanged: `meson test` green under ASan and UBSan, `just
upstream-regress` unchanged, `just bench-compare` within 10%. The embedding API is
proven in process: `tests/unit/test_embed.c` starts termo on an `openpty` pair, sees the
status line on the master, sends `C-b %` through it, reads `2` from
`display -p '#{window_panes}'`, resizes, detaches and stops, ASan clean through
`lua_close`. The wasm32 host is proven in Node: `tests/web/smoke.mjs` does the same
through the devices, plus a `run-lua` round trip; the sanitized wasm build runs it in
CI. The `.wasm` size is recorded in the plan.

## Decisions (2026-09-24)

1. **Explicit host layer, not a per-platform `main`.** The seam is a header and one file
   per host; the embedding API is native code tested on every commit.
2. **Rule 5 stands as written.** No WebAssembly runtime for plugins; a wasm32 build
   target of termo is a different thing and this intent says so once.
3. **The exec is not abstracted.** `host_pty_open` is the `fdforkpty` of today; the child
   sequence in `spawn_pane` and `job_run` is untouched.
4. **PUC Lua 5.1 on the wasm host**, `HAVE_LUAJIT` keeps its name (the macro means a Lua
   5.1 VM is linked; renaming ten sites is churn).
5. **Two items.** Core, build and CI here; site and playground in 008, which depends on
   this one.
