# Spec 004: C23 and POSIX.1-2024 modernisation

- Status: Approved 2026-09-07
- Intent: [`intent/004-c23-posix.md`](../intent/004-c23-posix.md)
- Plan: [`plan/004-c23-posix.md`](../plan/004-c23-posix.md)

## Compiler floor

GCC 14, Clang 20, Apple clang 21 (Xcode 26). `meson.build` sets `c_std=c23` only and probes,
at setup, a translation unit using `nullptr`, `constexpr`, a fixed-type enum and
`<stdckdint.h>`; failure is `error('termo needs C23: GCC 14+, Clang 20+ or Xcode 26+')`.

Features used in the tree: `[[noreturn]]`, `[[maybe_unused]]`, `[[fallthrough]]`,
`[[nodiscard]]`, `[[deprecated]]`, `[[gnu::format]]`, `[[gnu::packed]]`, `[[gnu::nonnull]]`,
`nullptr`, `bool`/`true`/`false`, `constexpr` objects, `static_assert`, `enum name : type`,
`ckd_add`/`ckd_mul`, `unreachable()`, `{}` initialisers, `typeof`.

Not used, because a target lacks them or they buy nothing here: `<stdbit.h>`,
`memset_explicit`, `char8_t`, `#embed`, `_BitInt`, `[[unsequenced]]`, decimal floats.

## Attributes

`src/compat/compat.h` stops defining `__unused`, `__dead`, `__packed`, `__weak` and the
`NEED_FUZZING` block. `src/core/termo.h` stops defining `printflike`. `src/core/xmalloc.h`
declares allocators with `[[gnu::format]]`, `[[gnu::nonnull]]` and `[[nodiscard]]` and drops
`__bounded__`. Every `/* FALLTHROUGH */` becomes `[[fallthrough]];` and
`-Wimplicit-fallthrough` joins the always-on warnings.

## Platforms and compat

Targets: Linux (glibc 2.38+ or musl), macOS 15+, FreeBSD 14, OpenBSD 7.7, NetBSD 10.
`meson.build` selects `osdep-{linux,darwin,freebsd,openbsd,netbsd}.c` and errors otherwise.

A shim stays in `src/compat/` only if at least one target lacks the function. Measured:

| Stays (why) | Goes (all targets have it) |
|---|---|
| `reallocarray`, `recallocarray`, `closefrom`, `explicit_bzero`, `freezero`, `getdtablecount`, `htonll`/`ntohll`, `fdforkpty`, `daemon` (macOS lacks) | `strlcpy`, `strlcat`, `strnlen`, `strndup`, `memmem`, `asprintf`, `getline`, `strsep`, `strcasestr`, `cfmakeraw`, `getdtablesize`, `setenv`, `clock_gettime`, `err.h` |
| `getprogname`, `strtonum`, `getpeereid`, `base64`, `setproctitle` (glibc lacks) | `forkpty-{aix,haiku,hpux,sunos}.c` (never built) |
| `vis`/`unvis` (macOS ABI differs), `imsg`, `getopt_long` (BSD semantics), `utf8proc` | `systemd.c` unless the `systemd` option is wired to `libsystemd` (it is: see plan) |

Every `HAVE_*` read by surviving code is produced by a Meson probe (`cc.has_function`,
`cc.has_header_symbol`, `cc.has_header`): `HAVE_SO_PEERCRED`, `HAVE_PROC_PIDINFO`,
`HAVE_PROC_PID`, `HAVE_PRCTL`, `HAVE_PR_SET_NAME`, `HAVE_FCNTL_CLOSEM`, `HAVE_DIRFD`,
`HAVE___PROGNAME`, `HAVE_PROGRAM_INVOCATION_SHORT_NAME`, `HAVE_SYS_SIGNAME`,
`HAVE_MALLOC_TRIM`, `HAVE_SYSTEMD`, `ENABLE_CGROUPS`. The rest (`HAVE_UTEMPTER`,
`HAVE_JEMALLOC`, autotools leftovers) are deleted with their branches. `IS_DARWIN` and
`IS_LINUX` go.

## Warnings

Always on: `-Wall -Wextra -Wimplicit-fallthrough -Wshadow -Wmissing-prototypes
-Wstrict-prototypes -Wvla -Wformat=2` once each is at zero; CI adds `-Dwerror=true`.
`-Wconversion` is measured and recorded, not enforced in this phase. Suppressions in
`supported_c_args` that no longer fire are removed.

## Checked arithmetic

Size computations before an allocation in `grid.c`, `screen.c`, `utf8.c`, `input.c` use
`ckd_add`/`ckd_mul` from `<stdckdint.h>` and `fatalx("size overflow")` on failure;
`xmalloc.h` gains `xmallocarray(n, size)`. `xreallocarray` already checks the product; the
work is the additions (`n + 1`, `off + size + codelen + 1`, `hsize + sy`).

## Typed `termo.h`

Function-like macros (`screen_size_x`, `screen_hsize`, `SCREEN_IS_ALTERNATE`,
`COLOUR_DEFAULT`, `KEYC_IS_*`, `MOUSE_*`) become `static inline` functions. Flag groups whose
field has one type become fixed-type enums with a `static_assert` on the size: `GRID_ATTR_*`
(`u_short`), `GRID_FLAG_*` (`u_char`), `GRID_LINE_*`, `MODE_*`, `PANE_*`, `CLIENT_*`
(`uint64_t`), `WINDOW_*`, `WINLINK_*`, `FORMAT_*`, `TTY_*`. The two anonymous key-code enums
become `enum : unsigned long long`. Numeric constants (`PANE_MINIMUM`, `UTF8_SIZE`, `KEYC_*`
masks) become `constexpr`. One commit per group.

## Convention for new code

Documented in `CLAUDE.md` and `CONTRIBUTING.md`, checked by `.clang-tidy` in nightly against a
baseline: `nullptr`, `bool`, `constexpr` over `#define` for typed constants, `[[nodiscard]]` on
functions returning resources or errors, `ckd_*` on size arithmetic, no direct
`__attribute__`, nothing new in `compat/` without a target that lacks it.

## Verification

- `meson test` green with `-Dwerror=true` on gcc-14, clang-20 and Apple clang 21 for every PR;
  FreeBSD nightly green at PR 2 and at close.
- `grep -rn '__unused\|__dead\|printflike\|FALLTHROUGH\|__attribute__' src` empty outside
  `compat/`.
- No `HAVE_*` orphan in either direction; no compat source Meson does not build.
- The 156 unit cases keep their expectations; `test_compat.c` covers every shim that stays.
