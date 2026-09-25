---
name: unit
description: Build and run the C unit tests (all, one module, or one case) and read failures.
---

`ninja -C build && ./build/tests/termo-test $ARGUMENTS`. Arguments: `[module [case-substring]]`,
for example `format` or `format expressions`. Output is TAP; a failing check prints
`# file:line: expr: got X, want Y` above its `not ok` line. A crash is not caught: read the
sanitizer report it prints. `TERMO_TEST_LOG=1` adds the server debug log (`termo-test-<pid>.log`),
which is where a `fatal()` message ends up.

To add a test: `TEST(module, function_property) { CHECK_EQ(...); }` in
`tests/unit/test_<module>.c` (see `tests/unit/test.h`). A new file goes in `unit_sources` and the
module list in `tests/meson.build`. Tests link the whole tree (`libtermo`), so call real
functions; the harness resets the option trees before every case.
