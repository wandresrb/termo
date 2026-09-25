# Intent 004: C23 and POSIX.1-2024 before Lua

- Status: Approved 2026-09-07
- Author: wandresrb

## Problem

termo compiles as C23 (`c_std=c23` since the Meson port) but the tree is written as the C99 of
tmux: 240 `__unused`, 11 `__dead`, 39 `printflike`, no `nullptr`, no `bool`, no
`static_assert`, no checked arithmetic. `compat/` and `osdep/` still carry AIX, HP-UX, SunOS,
Cygwin, Haiku and DragonFly, five compat sources Meson never builds, and 35 `HAVE_*` macros the
code tests that Meson never defines, so platform branches inside the shims are silently off.

Lua (`src/lua/`) would be written into that tree and then modernised again. Doing the language
and platform cleanup first means new code is born in the final style and the warning floor is
raised on a quiet tree.

## Outcome

- The tree uses the C23 features every target compiler supports: `[[...]]` attributes,
  `nullptr`, `bool`, `constexpr`, fixed-type enums, `<stdckdint.h>`, `unreachable()`.
- `compat/` holds only what a target platform lacks, measured against POSIX.1-2024; `osdep/`
  holds only Linux, macOS, FreeBSD, OpenBSD, NetBSD.
- Every `HAVE_*` the code reads is defined by Meson when true; none is dead.
- Warnings that the tree can hold at zero run under `-Werror` in CI.
- Compiler floor declared and enforced at `meson setup`: GCC 14, Clang 20, Apple clang 21.

## What this does not do

No `NULL` to `nullptr` sweep, no `u_int` to `uint32_t` sweep, no replacement of `queue.h`,
`tree.h`, `cmd-parse.y`, `gettimeofday` or `ioctl`. Nothing that is churn without a payoff, and
nothing that fights upstream cherry-picks for style alone.

## Cost accepted

Building from source needs GCC 14 / Clang 20 / Xcode 26. Debian 12, Ubuntu 22.04 and RHEL 9
need a newer toolchain. The binary, configs, protocol and every runtime behaviour are unchanged.
