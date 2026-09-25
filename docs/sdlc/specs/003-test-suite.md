# Spec 003: unit test suite

- Status: Approved 2026-09-06
- Intent: [`intent/003-test-suite.md`](../intent/003-test-suite.md)

## Linking model

`meson.build` builds `libtermo` (`static_library`) from every source except `src/core/main.c`,
including the compat shims chosen by configuration. It is an internal build artifact, not an
installed library: the same objects as before, archived once so more than one executable can link
them. The `termo` executable is `main.c` plus `libtermo`. All unit tests are one executable,
`tests/termo-test`, built from `tests/unit/*.c` plus `libtermo`.

Process-wide globals and helpers that lived in `main.c` (`global_options`, `socket_path`,
`get_timer`, `getversion`, `find_home`, ...) moved unchanged to `src/core/util.c`, because
`main.o` cannot be linked into a test binary. Nothing new may add global state.

Measured with `nm -u` on the built objects, the leaf modules pull in between 0 (`regsub`) and 127
(`format`) internal symbols. Linking the library gives every test its real dependencies with no
stub files. Isolation, where a test needs it, uses the linker's `--wrap=<symbol>` for that case.

## Framework: `tests/unit/test.h`, no dependency

C23 has no test facility; it has what a 60-line header needs:

- `TEST(module, name) { ... }` registers itself with `[[gnu::constructor]]`. Adding a test is
  adding the `.c` to `unit_sources` in `tests/meson.build`; no list of cases anywhere.
- `CHECK_EQ(a, b)` dispatches on type with `_Generic` (integers, unsigned, doubles, strings,
  pointers) and prints both values on failure. `CHECK(cond)`, `CHECK_NULL`, `CHECK_NONNULL`
  record and continue; the `REQUIRE*` forms return from the test.
- No signal handling: a crash dies with the sanitizer report and full stack, which is what you
  want to see.
- `#embed` is available for binary fixtures (VT corpora) when `test_input.c` needs them.

`tests/unit/main.c` runs `termo-test [module [case-substring]]` and prints TAP; Meson registers
one `test()` per module with `protocol: 'tap'`, so `meson test` shows every case.

## Harness

`tests/unit/harness.h`: `termo_test_init()` creates `global_environ`, the three option trees with
defaults from `options_table`, libevent and `socket_path`, once. `termo_test_reset()` recreates
the option trees before every test so nothing leaks between cases. The fuzzers'
`LLVMFuzzerInitialize` call the same `termo_test_init`.

Cases are named `<function>_<property>`; one property per case; no output on success.

## Test files and what they own

| File | Module | Must cover |
|---|---|---|
| `test_compat.c` | `src/compat/` | every shim through the name the tree uses, so a wrong libc-vs-shim choice fails here |
| `test_options.c` | `options.c`, `options-table.c` | table invariants, scopes and parent fallback, arrays, parsing |
| `test_cfg.c` | `cfg.c` | `load_cfg_from_buffer` on strings and on `etc/termo.conf`, every promised default |
| `test_format.c` | `format.c` | variables, each modifier, expressions incl. division by zero, recursion limits |
| `test_regsub.c` | `regsub.c` | groups, flags, invalid pattern, growth |
| `test_utf8.c` | `utf8/` | decoder state machine, invalid sequences, widths, vis, combined |
| `test_grid.c` | `grid/` | bounds, history, reflow, region ops, reader motion, string output |
| `test_colour.c` | `colour.c` | parse/print round trip, RGB split/join, palette |
| `test_style.c` | `style.c` | every attribute, malformed input leaves previous style intact |
| `test_key_string.c` | `key-string.c` | lookup both directions for the whole table |
| `test_arguments.c` | `arguments.c` | parse against a real `cmd_entry` table, escaping |
| `test_layout.c` | `layout/` | parse/dump round trip, invalid checksum, minimum sizes |
| `test_screen_write.c` | `screen/write.c` | wrap, wide cells, insert/delete, regions, alternate |
| `test_input.c` | `input.c` | SGR, CSI edge cases, OSC, DCS/APC, DA reply, garbage |

Screen-level assertions use `grid_string_cells` line by line as the oracle.

## Build and CI

- `tests/meson.build`: `termo-test` executable, one `test()` per module, always built.
- `ci.yml`: `meson test` already runs suite `unit`; no extra packages.
- Close criterion: every row of the table above has named cases that prove its "Must cover"
  column; the list per row is Step 5 of the plan. No coverage gate and no coverage job in CI.
- Coverage as information, measured locally when the list is revised:
  `brew install gcovr` (macOS), `meson setup build-cov -Db_coverage=true -Db_sanitize=none
  -Dbuildtype=debug`, `meson test -C build-cov --suite unit`, `ninja -C build-cov coverage-text`,
  report in `build-cov/meson-logs/coverage.txt`. Meson drives `llvm-cov gcov` for clang.

## Trade-offs

- Real dependencies instead of mocks means a failure in `utf8` can surface in `test_grid`. That is
  accepted: the tests run in seconds and the failing check names file and line.
- A tiny own harness instead of cmocka: no fixtures beyond a `setup()` call, no mocks. If mocks
  are ever needed, `--wrap` is a linker feature and works the same.
- `libtermo` makes the link of the main binary a two-step; incremental build time is unchanged
  (thin archive, same objects).
