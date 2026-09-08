# Spec 005: CI on builder images, e2e in pytest, upstream regress on demand

- Status: Approved 2026-09-08
- Intent: [`intent/005-ci-and-e2e.md`](../intent/005-ci-and-e2e.md)
- Plan: [`plan/005-ci-and-e2e.md`](../plan/005-ci-and-e2e.md)

## Facts the design rests on

- ASan and LSan support `vm.mmap_rnd_bits=32` since LLVM (April 2023) and GCC 14 ships that
  runtime; the `sysctl` step is unnecessary and impossible inside `container:`. TSan and MSan
  are not used.
- Private-repo Linux runners have 2 vCPU and 8 GB; macOS runners are 3 M1 cores at 10x.
- `container.image` accepts the `github` and `needs` contexts and no functions, so an image
  tag has to come from a previous job's output.
- The repository is private without Advanced Security: no SARIF upload can land, so CodeQL and
  Scorecard are removed until the repository is public; zizmor reports to the log.
- Control mode: `%begin`/`%end` numbers come from a server-wide counter (`src/cmd/queue.c`),
  so correlation is positional; an empty stdin line exits the client (`src/core/control.c`);
  notifications are never emitted inside an open block. `EDITOR`/`VISUAL` containing `vi` flip
  `mode-keys` (`src/core/main.c`); `$XDG_CONFIG_HOME/termo/lua` is first on `package.path`.
- The 129 scripts in `tests/regress/` are upstream's with `TEST_TMUX` renamed; termo still
  reads `TEST_TMUX` and `TMUX_TMPDIR`, so upstream's tree runs unmodified against `build/termo`.

## CI

Images, built by `.github/workflows/image.yml` (`workflow_call`), tag = first 16 hex digits of
`sha256sum ci/Dockerfile.<x>`, pushed to `ghcr.io/<repo>-ci-<x>:<tag>` only when
`docker manifest inspect` does not find it. The first push always comes from the workflow so the
package is linked to the repository.

- `ci/Dockerfile.ubuntu`: `ubuntu:24.04`, gcc-14, clang-20 + clang-tidy-20 + `libclang-rt-20-dev`
  + `llvm-symbolizer` from apt.llvm.org, Meson 1.12 from pip, ninja, bison, ccache, libevent,
  ncurses + `ncurses-term`, utf8proc, luajit, libsystemd, procps, python3 + pytest. Root.
- `ci/Dockerfile.alpine`: `alpine:3.22`, gcc 14, musl-dev, Meson 1.8 from apk, samurai, bison,
  libevent, ncurses, utf8proc, luajit, python3 + py3-pytest, bash and git. No sanitizers.

`ci.yml` (`push: main`, `pull_request`, cancel-in-progress): `lint` (commit subjects, PR only)
and `image`, then per compiler (`gcc-14`, `clang-20`) in the Ubuntu container (`credentials`
with `GITHUB_TOKEN`, `packages: read`) four stages, each a job that `needs` the previous:
`build` (ccache via `actions/cache`, `meson setup -Db_sanitize=address,undefined
-Dbuildtype=debugoptimized -Dwerror=true -De2e=enabled`, `ninja`, then the build tree without
`*.p` objects and `*.a` uploaded as an artifact), `unit` (`meson test --suite unit
--no-rebuild`), `smoke` (`--suite lua --suite smoke`: the Lua specs and the `cli` e2e module),
`e2e` (`--suite e2e --no-suite smoke`, logs and `/tmp/termo-{asan,ubsan}.*` uploaded on
failure). Every stage runs in the same image at the same workspace path, which is what lets the
Meson build directory move between jobs. No macOS.

`nightly.yml` (cron, dispatch): `image`, then `fuzz` (clang-20, 10 min per target), `variants`
(`no-luajit-no-utf8proc`, `sixel-release`), `clang-tidy` in the Ubuntu image; `alpine` in the
Alpine image (`meson compile`, `meson test`); `freebsd` on `vmactions/freebsd-vm` 15.1 with
luajit and pytest. NetBSD and OpenBSD later.

`zizmor.yml`: job permissions include `contents: read`; action pinned by SHA; log output.
`lintcommit.yml`, `codeql.yml`, `scorecard.yml`: removed (folded into `ci.yml`; require GHAS).

`meson.build`: `meson_version: '>= 1.4.0'` (`c_std=c23` needs it).

## e2e layer (`tests/e2e/`)

`termo.py` is the harness; `conftest.py` the fixtures; one `test_<area>.py` per area.

- `expect(pred, timeout, what, describe)`: polls every 50 ms until `pred()` is truthy; the
  deadline is `5 s × TERMO_E2E_TIMEOUT_SCALE` (Meson sets 3 under ASan); on timeout raises
  `ExpectTimeout` with `what` and `describe()`, the last observation. No `time.sleep` anywhere.
- `Server(binary, name)`: socket `e2e-<pid>-<n>`; environment without `TMUX`, `TERMO`, `*_PANE`,
  `EDITOR`, `VISUAL`, `LC_ALL`, `LANG`, with `SHELL=/bin/sh`, `LC_CTYPE=C.UTF-8`, `TERM=xterm`,
  `MallocNanoZone=0`, `TERMO_RUNTIME`, `XDG_CONFIG_HOME` pointing at an empty directory.
  `start(*args, conf, lua_init, size, session, command)` runs `new-session -d`; `cmd`, `out`,
  `fmt`, `option`, `capture`, `pane_ids`, `popen`, `attach_control`, `attach_pty`, `nest`,
  `kill` (kill-server, tracked children, `reap_leftovers`, sanitizer reports since start).
- `expect_fmt(server, fmt, want, target)`, `expect_screen(server, target, want)`: the latter
  polls `capture-pane -p [-e]` until every row matches (`rstrip`, `*` wildcard, `{MATCH:re}`)
  and the remaining rows are blank; failure shows a unified diff of the last capture.
- `Control`: `termo -C attach -f no-output`; a reader thread parses guard blocks with a stack
  (fake `%begin` lines printed by commands fold into the parent) and queues notifications with a
  sequence number; `run(cmd)` brackets the command with a `display -p <token>` so its block is
  found positionally, extra blocks go to `Block.extra`; `expect(kind, pred, since)`,
  `expect_output(pane, text)` with `\ooo` decoded; `close()` writes an empty line and waits for
  `%exit`. Negative assertions only behind an ordering fence.
- `Pty`: `pty.fork` + `execv(termo -L name attach)`, `TIOCSWINSZ`, `send_keys` from a `KEYS`
  table (`C-x`, `M-x`, names, text), `wait_for(regex)` over the raw bytes; the attach handshake
  is `#{client_pid}` in `list-clients`, then a status-line marker.
- Meson: option `e2e` (feature, auto; CI enables it); one `test('e2e-<module>')` per file with
  `python3 -m pytest -q -p no:cacheprovider tests/e2e/test_<m>.py --termo <exe>`, env
  `TERMO_BIN`, `TERMO_RUNTIME`, `TERMO_HAS_LUA`, `TERMO_E2E_TIMEOUT_SCALE`,
  `TERMO_E2E_UNDER_MESON`, `depends: termo_exe`, `timeout: 120`, `suite: 'e2e'`; modules run
  in parallel, each server has its own socket. Lua modules skip themselves by marker when the
  binary has no LuaJIT. `pytest_sessionfinish` maps "no tests collected" to success under Meson
  so `--test-args='-k name'` works.
- Dependencies: pytest only. The screen oracle is the server's grid via `capture-pane`; the pty
  reader is for handshakes and overlay text; `nest()` captures a whole client screen through an
  outer server.
- Debugging: `TERMO_E2E_KEEP=1` leaves a failed test's server running and prints the `attach`;
  `TERMO_E2E_VERBOSE=1` starts servers with `-vv` in the test's tmp dir.

## Upstream regress on demand

`tools/regress-runner.py` (today's `tests/regress/runner.py`) with `--dir`, `--xfail`
(default `tools/regress-xfail`) and `--log-dir`; `just upstream-regress [scripts]` fetches
`upstream/master:regress/` into `build/upstream-regress/` and runs it against `build/termo`.

## Verification

`meson test -C build --print-errorlogs` green under ASan with unit, lua and every e2e module;
`-Dluajit=disabled` build green with the Lua modules skipped; `grep -r 'sleep(' tests/e2e`
empty; first CI run builds both images and passes `gcc-14` and `clang-20`; nightly by dispatch
green on fuzz, variants, clang-tidy, alpine and freebsd; `just upstream-regress
screen-redraw-tiled` runs upstream's script.
