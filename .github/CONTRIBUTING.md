# Contributing

termo follows the process in `docs/sdlc/`: an intent (what and why), a spec
(how), and a plan (files, order, verification) before implementation. Small
fixes need none of that, just a PR.

## Pull requests

Fork the repository, branch from `main`, push to your fork and open a PR against
`main`. CI is the gate: a PR merges when `commit messages`, `gcc-14`, `clang-20`
and `macos` are green, which means `meson test` (unit, Lua specs, e2e) passes
under ASan and UBSan with `-Werror` on Linux gcc and clang and on macOS. Nobody
merges around a red check, and review threads are resolved before merging.
`docs/ci.md` explains what each check runs.

## Before opening a PR

You need a C23 compiler: GCC 14+, Clang 20+, or Xcode 26+.

```sh
meson setup build -Db_sanitize=address,undefined -Dbuildtype=debugoptimized
ninja -C build
meson test -C build
```

`meson test` runs the C unit tests, the Lua specs and the pytest e2e suite
(`tests/e2e/`, needs pytest; `just build && just test` does the same). Everything
must be green and sanitizer clean. A change in a leaf module (`utf8/`, `grid/`,
`input/`, `format.c`, `options.c`, `cfg.c`, `compat/`) comes with a unit test in
`tests/unit/test_<module>.c`; see `tests/unit/test.h`. A change in behaviour a
user sees comes with an e2e test. Run one thing at a time with:

```sh
./build/tests/termo-test format expressions
pytest tests/e2e -k menu --termo build/termo
just upstream-regress alerts      # upstream tmux's regress/, before a release
```

## The gate locally

The Linux checks run inside `ghcr.io/wandresrb/termo-ci-ubuntu`, a public image
tagged by the sha256 of `ci/Dockerfile.ubuntu`. `just ci-local` runs the same
configure, build and test steps in that image with Docker or Podman, whichever
is installed; `just ci-local clang-20` does it with clang. The build tree is
`build-ci/`.

## Changing the CI image

`ci/Dockerfile.ubuntu` and `ci/Dockerfile.alpine` are the toolchain; jobs
install nothing. A change to either is a new tag, built and pushed by the
workflow on the first run that sees it. A PR from a fork runs with a read-only
token and cannot push, so its `image` job fails and says so: keep Dockerfile
changes out of fork PRs, or ask a maintainer to push your branch to the
repository, where the image gets built and the PR then passes. Never push an
image from a laptop; the workflow owns the package.

## Code

C23. New code: `nullptr`, `bool`, `constexpr` constants, fixed-type enums for flags,
`[[nodiscard]]` where a return must not be dropped, `ckd_add`/`ckd_mul` on sizes, no VLAs, no
direct `__attribute__` (use `[[gnu::...]]`). Do not restyle existing code; a change in a leaf
module comes with its unit test. `-Werror` is on in CI with the warning set in `meson.build`.

## Commits

One line, imperative, at most 72 characters, no trailing period, no body, no
trailers; the `commit messages` check enforces it on every commit in the PR.
The PR description carries the reasoning and links the intent/spec/plan it
implements.

## Bugs

Use the bug report form. Include `termo -V`, your platform, `$TERM` inside and
outside termo, and a reproduction with `termo -Ltest -f/dev/null` so your own
config is not involved. Attach `termo-server-PID.log` and `termo-client-PID.log`
from `termo -vv` if the problem is not obvious. Vulnerabilities go through
private reporting, see `docs/SECURITY.md`.
