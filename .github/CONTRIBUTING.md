# Contributing

termo follows the process in `docs/sdlc/`: an intent (what and why), a spec
(how), and a plan (files, order, verification) before implementation. Small
fixes need none of that, just a PR.

## Before opening a PR

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

## Commits

One line, imperative, no body, no trailers. The PR description carries the
reasoning and links the intent/spec/plan it implements.

## Bugs

Include `termo -V`, your platform, `$TERM` inside and outside termo, and a
reproduction with `termo -Ltest -f/dev/null` so your own config is not involved.
Attach logs from `termo -vv` if the problem is not obvious.
