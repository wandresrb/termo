# Plan 007: termo as a host-neutral core, wasm32 as the first foreign host

- Status: Draft 2026-09-24
- Intent: [`intent/007-embed.md`](../intent/007-embed.md)
- Spec: [`specs/007-embed.md`](../specs/007-embed.md)

Steps land as merges into `refactor/to-termo`, one branch `embed/007`, each step green
locally (`meson test` under ASan and UBSan; `just upstream-regress` and `just
bench-compare` on the steps that touch `spawn.c`, `job.c`, `server.c` or `client.c`)
and rebased before the next. Steps 1 and 2 are native only and prove the seam without a
browser; 3 to 5 are the web host; 6 closes. Item 008 starts after step 4 (it needs
`build-web/termo.js`) and its CI after step 5.

| Step | Contents | Proof |
|---|---|---|
| 0 | These three documents; `ROADMAP.md` Phase 6 | — |
| 1 | **Host contract.** `src/host/host.h`, `src/host/host-posix.c` (`osdep_*`, `fdforkpty`, `tty_term_read_list` behind it; `getptmfd` moves here from `compat/fdforkpty.c`); `meson.build` adds `host-posix.c` to the five branches; `termo.h` includes `host/host.h` and declares `server_init`, `server_loop`, `server_client_identified`, `server_client_resized`, `options_create_defaults`; `server.c` and `client.c` split as spec §3; `spawn.c:479`, `job.c:116`, `client.c:326`, `main.c`, `names.c`, `harness.c` call the host; `options.c` gains `options_create_defaults` and `harness.c` uses it; `docs/SYNCING.md` | `meson test` green under ASan and UBSan on macOS and in the Ubuntu image with gcc-14 and clang-20; `just upstream-regress` same result set as the sha before; `just bench-compare` within 10%; `git grep -n 'fdforkpty\|tty_term_read_list\|osdep_' src/ -- ':!src/host' ':!src/osdep' ':!src/compat' ':!src/core/termo.h'` is empty |
| 2 | **Embedding API.** `src/host/embed.c` (spec §2), `tests/unit/test_embed.c` with the seven cases of spec §5, `tests/meson.build` (`unit_sources`, `unit_modules`); `docs/embed.md` | `./build/tests/termo-test embed` passes under ASan and UBSan with and without `-Dluajit`; `meson test --suite unit` green with `embed` in the module order before `lua` and after |
| 3 | **Capabilities table.** `tools/gen-caps.c`, Meson target `gen-caps` (native only), `src/web/caps-xterm-256color.c`, the `caps fresh` step in the `gcc-14` job | `ninja -C build gen-caps > /tmp/caps.c && diff /tmp/caps.c src/web/caps-xterm-256color.c` empty; the table has every `tty_term_codes[]` entry that `xterm-256color` defines |
| 4 | **Web host.** `subprojects/libevent.wrap`, `packagefiles/libevent/{meson.build,event-config.h}`; `subprojects/lua51.wrap`, `packagefiles/lua51/meson.build`; `src/lua/runtime.c` `__has_include`; `ci/emscripten.ini`; `meson.build` `emscripten` branch, conditional dependencies, the `termo.js` executable (spec §4.4); `src/host/host-web.c`, `src/web/main.c`, `src/web/library.js`; `tests/web/smoke.mjs` and its Meson registration; `justfile` `wasm`; `.gitignore` unchanged (`build*/`) | `just wasm` with `-Dwerror` produces `build-web/termo.js`, `termo.wasm`; `meson test -C build-web --suite web` passes on the sanitized build; the non-sanitized `.wasm` size is recorded in Progress; a native `meson setup build` is byte-identical in `compile_commands.json` to the sha before (the branch adds nothing to POSIX builds) |
| 5 | **CI.** `ci/Dockerfile.web`; `image.yml` output `web`; `ci.yml` job `web`; `docs/ci.md`; `just ci-web` (the gate's steps in the image) | gate green on a push; `just ci-web` green locally; the image tag is reproducible |
| 6 | **Close.** `CLAUDE.md` section and status line; plan 006 gains the constraint that removing a C twin requires the wasm host to build the Rust module for `wasm32-unknown-emscripten`; Progress section here | — |

## Files

Create: `src/host/host.h`, `src/host/host-posix.c`, `src/host/host-web.c`,
`src/host/embed.c`, `src/web/main.c`, `src/web/library.js`,
`src/web/caps-xterm-256color.c`, `tools/gen-caps.c`, `tests/unit/test_embed.c`,
`tests/web/smoke.mjs`, `ci/emscripten.ini`, `ci/Dockerfile.web`,
`subprojects/libevent.wrap`, `subprojects/packagefiles/libevent/{meson.build,event-config.h}`,
`subprojects/lua51.wrap`, `subprojects/packagefiles/lua51/meson.build`, `docs/embed.md`.

Modify: `meson.build`, `tests/meson.build`, `justfile`, `src/core/termo.h`,
`src/server/server.c`, `src/server/client.c`, `src/core/spawn.c`, `src/core/job.c`,
`src/client/client.c`, `src/core/main.c`, `src/core/names.c`, `src/config/options.c`,
`src/compat/fdforkpty.c`, `src/lua/runtime.c`, `tests/unit/harness.c`,
`.github/workflows/{ci,image}.yml`, `docs/{ci,SYNCING}.md`, `CLAUDE.md`, `ROADMAP.md`,
`docs/sdlc/plan/006-rust.md` (one constraint line).

Reuse untouched: `server_client_create`, the identify `switch`, `tty_init`/`tty_open`/
`tty_add` and all of `tty.c`, `window_pane_set_event` and the pane I/O path,
`input_init`, `cmdq_*` and `cmdq_capture`, `load_cfg_from_buffer`, `start_cfg`,
`tty_term_create` and the overrides, `proc_start`, `osdep-*.c`, `fdforkpty.c`'s
`fdforkpty`, the unit harness and `test.h`.

## Risks

- **`poll` on Emscripten devices.** Verified from the JS sources, not by running; if
  `poll` is not consulted for a registered device, the read callback fires every tick with
  `EAGAIN`, which is correct but wasteful; the smoke counts reads per idle tick and the
  fix is on the device side only.
- **The unit binary hosts a server.** `test_embed` brings up server state in the process
  the other modules share; `termo_embed_stop` must leave `global_options` and the trees
  as `termo_test_reset` expects, and `test_lua` must still pass after it. If that proves
  brittle, `embed` becomes its own executable in `tests/meson.build` (same sources plus
  `harness.c`), not a reason to weaken `stop`.
- **Terminfo in the unit test.** `host_term_caps("xterm-256color")` needs a terminfo
  database; the CI images have `ncurses-term`, macOS ships one; the test skips with the
  cause if it is absent rather than failing.
- **libevent 2.1.12 on emcc.** The source list is fixed by hand and `event-config.h` is
  written by hand; a missing symbol shows at link. `evsig` (signal.c) is compiled but
  never armed (no `proc_set_signals` on the embed path).
- **Emscripten's `wcwidth`.** musl's tables may be older than the hosts'; the smoke
  checks one wide character; `utf8proc` from WrapDB is the fallback, or intent 006 axis 6.
- **Plan 006 removing C twins.** Recorded in step 6 as a constraint on plan 006, not
  solved here.
- **Size.** `-Oz` and no `ASSERTIONS` for the published build; the number is recorded,
  not gated, in this item; item 008 sets the page budget.

## Close

Steps 0 to 6 merged, Progress written, `web` in the ruleset; item 008 starts on
`build-web/termo.js`.
