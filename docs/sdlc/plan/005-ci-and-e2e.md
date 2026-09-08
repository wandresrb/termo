# Plan 005: CI on builder images and the e2e suite

- Status: Approved 2026-09-08; steps 0 to 7 landed 2026-09-08, first CI run pending the push
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
- `resize-pane -x 0` on a floating pane said `size size is too big or too small`:
  `src/cmd/pane/resize.c` prefixed a cause that already carried the word. Fixed; the e2e test
  asserts the exact message. Upstream has the same lines.
