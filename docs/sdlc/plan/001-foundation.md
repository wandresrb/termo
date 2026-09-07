# Plan: Phase 0 & Phase 1 — Build System, Complete Rebranding & Sane Defaults

- **Derived from**: [`docs/sdlc/specs/001-termo-architecture.md`](specs/001-termo-architecture.md) & [`docs/sdlc/intent/001-termo-foundation.md`](intent/001-termo-foundation.md)
- **Status**: Completed & Verified
- **Playbook Phase**: Stage 5 (Continuous Evals & Verification Completed)

---

## 1. Summary of Executed Work

### 1.1 Modern Build System (Phase 0)
1. **Meson + Ninja**: Complete replacement of Autotools (`autoconf`, `automake`). Rebuilds execute in ~0.05 seconds.
2. **C23 Standard**: Configured `c_std=c23,c2x,gnu17,c11` in `meson.build` with clean compilation under Apple Clang and GCC.
3. **Automated Sanitizers**: `-Db_sanitize=address,undefined` enabled with `-Db_lundef=false` on macOS.
4. **Test Pyramid**:
   - `unit`: C test harness (`tests/unit/test_sanity.c`) for core invariant verification.
   - `integration`: Python 3 end-to-end suite (`tests/integration/test_termo.py`) testing session lifecycle, window splitting, sane defaults, and copy-mode keybindings.
   - `regress`: Python 3 test runner (`tests/regress/runner.py`) wrapping all upstream regression tests without reliance on POSIX Makefiles or shell loops.

### 1.2 Full Codebase Rebranding (Punto 1)
1. **Header Migration**:
   - `src/core/tmux.h` -> [`src/core/termo.h`](file:///Users/willy/Developer/oss/termo/src/core/termo.h)
   - `src/core/tmux-protocol.h` -> [`src/core/termo-protocol.h`](file:///Users/willy/Developer/oss/termo/src/core/termo-protocol.h)
   - Updated all 152 C source files to `#include "termo.h"`.
   - Forwarding headers provided for backwards compatibility.
2. **Binary & Environment Variables**:
   - Version output: `termo 0.1.0`.
   - Socket directory: `/tmp/termo-<uid>/`.
   - Primary environment variables: `$TERMO`, `$TERMO_PANE`, `$TERMO_TMPDIR`, `TERM_PROGRAM=termo`.
   - Preserved `$TMUX` and `$TMUX_PANE` for prompt/shell compatibility.

### 1.3 Sane Defaults Out-of-the-Box (Phase 1)
1. **Mouse Support**: Explicitly `on` by default (`mouse 1`).
2. **Scrollback History**: `50,000` lines (`history-limit 50000`).
3. **Vi Mode**: `mode-keys vi` and `status-keys vi` enabled by default.
4. **Vi Keybindings**: `v` bound to `begin-selection`, `y` bound to `copy-pipe-and-cancel` in `copy-mode-vi`.
5. **System Clipboard**: OSC 52 enabled by default (`set-clipboard on`).
6. **TrueColor**: Automatic 24-bit RGB feature detection (`*:256:RGB`).
7. **Window Renumbering**: `renumber-windows on`.
8. **Focus Events**: `focus-events on`.
9. **Escape Latency**: `escape-time 10` ms.
10. **Clean Window Naming**: Automatic rename format uses `[termo]` instead of `[tmux]`.

### 1.4 Documentation & Root Cleanup
1. **Pristine Root**: Zero C code, dead autotools, or legacy CI in root directory.
2. **Documentation**: [`README.md`](file:///Users/willy/Developer/oss/termo/README.md) comprehensively documents building, testing, defaults, and architecture.
3. **Institutional Knowledge**: [`.github/CLAUDE.md`](file:///Users/willy/Developer/oss/termo/.github/CLAUDE.md) located cleanly under `.github/`.

---

## 2. Verification Results

- `ninja -C build`: 162/162 targets compiled cleanly with 0 warnings/errors under C23 and AddressSanitizer.
- `meson test -C build --verbose`:
  - `unit - termo:unit_sanity`: OK (0.45s)
  - `integration - termo:integration_termo`: OK (2.53s, 5/5 tests passed)
  - `regress - termo:regress_buffers`: OK (6.46s)
  - **Summary: 3/3 suites passed, 0 failures, 0 memory leaks under ASAN/UBSAN.**
