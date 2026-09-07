# Contributing

termo follows the process in `docs/sdlc/`: an intent (what and why), a spec
(how), and a plan (files, order, verification) before implementation. Small
fixes need none of that, just a PR.

## Before opening a PR

You need a C23 compiler: GCC 14+, Clang 20+, or Xcode 26+.

```sh
meson setup build -Db_sanitize=address,undefined -Dbuildtype=debugoptimized
ninja -C build
meson test -C build
```

`meson test` runs the C unit tests, the Python integration suite and all
regression scripts under `tests/regress/`. Everything must be green and
sanitizer clean. A change in a leaf module (`utf8/`, `grid/`, `input/`,
`format.c`, `options.c`, `cfg.c`, `compat/`) comes with a unit test in
`tests/unit/test_<module>.c`; see `tests/unit/test.h`. Run one thing at a time
with:

```sh
./build/tests/termo-test format expressions
python3 tests/regress/runner.py build/termo alerts.sh
```

## Code

C23. New code: `nullptr`, `bool`, `constexpr` constants, fixed-type enums for flags,
`[[nodiscard]]` where a return must not be dropped, `ckd_add`/`ckd_mul` on sizes, no VLAs, no
direct `__attribute__` (use `[[gnu::...]]`). Do not restyle existing code; a change in a leaf
module comes with its unit test. `-Werror` is on in CI with the warning set in `meson.build`.

## Commits

One line, imperative, no body, no trailers. The PR description carries the
reasoning and links the intent/spec/plan it implements.

## Bugs

Include `termo -V`, your platform, `$TERM` inside and outside termo, and a
reproduction with `termo -Ltest -f/dev/null` so your own config is not involved.
Attach logs from `termo -vv` if the problem is not obvious.
