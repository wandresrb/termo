# Plan 005: CI on builder images and the e2e suite

- Status: Approved 2026-09-08; steps 0 to 8 landed 2026-09-08
- Intent: [`intent/005-ci-and-e2e.md`](../intent/005-ci-and-e2e.md)
- Spec: [`specs/005-ci-and-e2e.md`](../specs/005-ci-and-e2e.md)

Steps on the single working branch, each green locally (`meson test`: unit, lua, e2e) before the
next; from step 3 on, green on the PR gate too.

| Step | Contents | Proof |
|---|---|---|
| 0 | GitHub default branch `master` → `main`; local refs; PR #1 retargeted | `gh repo view` says `main` |
| 1 | These three documents | — |
| 2 | `tests/e2e/`: `pytest.ini`, `conftest.py`, `termo.py` (`Server`, `expect`, `expect_fmt`, `expect_screen`, `reap_leftovers`), `test_cli.py`, `test_session.py`; option `e2e`; per-module `test()` registration; `tests/integration/test_termo.py` deleted | `meson test --suite e2e` green, `pytest tests/e2e -k session` selects |
| 3 | `ci/Dockerfile.ubuntu`, `ci/Dockerfile.alpine`, `image.yml`, `ci.yml`, `nightly.yml`, zizmor patched, `lintcommit.yml`/`codeql.yml`/`scorecard.yml` removed, `meson_version >= 1.4` | first run builds both images; `gcc-14` and `clang-20` jobs green with ASan in the container |
| 4 | `Pty`, `attach_pty`, `fixtures/`, `test_lua_ui.py` (9), `test_lua.py` (4), Lua skips; `tests/integration/` deleted | green with and without LuaJIT |
| 5 | `Control`, block parser, `test_control.py` (10) | green |
| 6 | `capture`, `nest`, `test_screen.py` (7), `test_layout.py` (7) | green |
| 7 | `tools/regress-runner.py` (`--dir`, `--xfail`, `--log-dir`), `tools/regress-xfail`, `justfile`, `tests/regress/` deleted, docs | `just upstream-regress screen-redraw-tiled` runs upstream's script against `build/termo` |
| 8 | After the first run and the repository going public: `ci.yml` one job per compiler with the stages as steps, `macos` last; `image.yml` fork guard; `codeql.yml`, `scorecard.yml` back, `zizmor.yml` with SARIF; `dependabot.yml`, `CODEOWNERS`, issue forms, `CONTRIBUTING.md`, `docs/ci.md`; nightly `runs-on` from `TERMO_LINUX_RUNNER`; API: secret scanning, push protection, Dependabot, private vulnerability reporting, the `main` ruleset | gate green with `gcc-14`, `clang-20`, `macos`; scans in Security → Code scanning; a direct push to `main` rejected |

## Risks

- Image pull auth on the first run: the package must be created by the workflow to be linked.
- LSan inside `container:`: expected to work with gcc-14/clang-20 runtimes; fallbacks are
  `--cap-add SYS_PTRACE`, then `detect_leaks=0`, then the VM with `pipx install meson`.
- Control-mode block numbers are global to the server: correlation is positional (bracketed
  `run()`), never by number.
- The Alpine container runs JavaScript actions on the runner's musl node; verify checkout and
  upload-artifact there on the first nightly.
- `send-keys -K` against a control client and a handful of exact numbers (split geometry,
  `history_size`, the sgr0 form of `capture-pane -e`) are verified against `build/termo` while
  writing each test.

## What the first container runs found (podman, arm64 Linux, 2026-09-08)

The gate had only ever run on macOS with Apple clang. Inside `ci/Dockerfile.ubuntu`:

- gcc-14 with `-Wformat=2 -Werror` stopped on 20 diagnostics. Real: `src/lua/format.c` used
  `isalnum` without `<ctype.h>` (macOS includes it transitively). Fixed in code: `setproctitle`
  builds the 16-byte name with `strlcpy`/`strlcat` instead of a truncating `snprintf`;
  `stravis` keeps the `realloc` result in a local; `server_client_set_progress_bar` copies the
  struct instead of `memcpy`; `control_write_guard` is `[[gnu::nonnull(2)]]` (UBSan's inline
  null check made gcc see a null format argument); `CHECK`/`CHECK_NONNULL` are a call, not a
  conditional expression; the `reallocarray` overflow case uses a `volatile` size. Turned off for
  gcc: `-Wmaybe-uninitialized` (ten upstream sites initialised on paths it cannot see) and
  `-Wformat-y2k` (`format_pretty_time` prints `%y` on purpose).
- clang-20: `setproctitle` needed `[[gnu::format(printf, 1, 2)]]` for `-Wformat-nonliteral`.
- aarch64 Linux has unsigned `char`: `utf8_cstrwidth` counted an invalid lead byte as width 1
  there and 0 on signed-char platforms. Now `*s < 0x7f`, like `utf8_sanitize`; upstream has the
  same inconsistency.
- glibc's `reallocarray` under ASan aborts on overflow instead of returning `NULL`; the unit
  case now exists only where the compat shim is compiled.
- LeakSanitizer, which macOS never runs, found that the command parser leaked its stack on a
  syntax error (`statement`, tokens): the grammar had no `%destructor`. Added for every
  allocated value, as symbol lists because Apple's bison is 2.3.
- gcc-14's `libasan.so.8` on aarch64 exports interceptors for `__b64_ntop`/`__b64_pton` that
  fail to find glibc 2.39's real symbols, so every `b64_*` call in the sanitised gcc build
  jumped to NULL (`compat` and `input` modules). termo now always compiles its own
  `compat/base64.c` and no longer links libresolv; the probe is gone.
- `resize-pane -x 0` on a floating pane said `size size is too big or too small`:
  `src/cmd/pane/resize.c` prefixed a cause that already carried the word. Fixed; the e2e test
  asserts the exact message. Upstream has the same lines.

## What the first GitHub run found (run 34267429093, 2026-09-08)

Every stage green, `commit messages` red on the branch's own history (squashed at merge). The
gate took 258 s for about 90 s of work: each of the eight stage jobs paid 18 to 27 s to pull the
967 MB image, 7 s of runner setup, a checkout, an artifact upload or download and 8 to 10 s of
scheduler between dependent jobs, about 35 s per boundary. The repository went public the same
day, which made standard runners free (macOS included) and Linux 4 vCPU. Decision: stages as
steps in one job per compiler (the job graph gave the same signal, first red stage, for 100 s
more), macOS back as the last job, and the public-repository set: rulesets, CodeQL, Scorecard,
zizmor SARIF, Dependabot, security settings, community files. Step 8 above.

A note on incremental builds, since the question came up: a hosted runner cannot have one. The
VM is new per job and the checkout gives every source today's mtime, so a restored `build/` is
dirty to ninja. ccache in `actions/cache` (content-addressed, so mtimes do not matter) is the
remote cache that works there; a self-hosted runner with a persistent workspace and a host
ccache with `base_dir` has real incrementality, and `docs/ci.md` has the recipe together with
the rule that it never serves `pull_request`.

## What the one-job-per-compiler run found (run 34287151877, 2026-09-08)

Gate green, 7 min 25 s end to end, with both images rebuilt (101 s, the OCI labels changed both
tags) and a cold ccache: `gcc-14` 82 s (container init 19 s, build 40 s, unit, smoke, e2e 10 s),
`clang-20` 184 s (container init 125 s pulling the same image, build 39 s), `macos` 141 s (brew
14 s, build 52 s, e2e 45 s). The critical path is image, then the slower Linux job, then macOS;
the next run with the tags present and ccache warm is the number to compare against 258 s. The
two Linux pulls of one image differed by 106 s on GitHub's side; nothing in the workflow drives
that.

CodeQL on the PR reported 15 alerts, because the PR diff is the whole tree. Five were accepted
and fixed in code: `window/clock.c` called `localtime` three times for one instant (now one
`localtime_r`), and `core/log.c` and `core/prompt-history.c` created files through `fopen`,
mode by umask, for data that is sensitive (the `-vv` log carries every byte the panes receive;
prompt history can hold a password): now `open(..., 0600)` plus `fdopen`. Ten were dismissed as
false positives with the reason on each alert: `%u` given `u_int` operands (`screen/write.c`),
the client opening the file the user named (`client/file.c`), `place[1]` in OpenBSD's
`getopt_long` after `*place` was checked, and `sscanf` results read only behind their count
check with `k` initialised (`core/colour.c`).
