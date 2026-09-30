# Plan 004: C23 and POSIX.1-2024 modernisation

- Status: Approved 2026-09-07; steps 0 to 6 landed, closed 2026-09-08 (see Progress)
- Intent: [`intent/004-c23-posix.md`](../intent/004-c23-posix.md)
- Spec: [`specs/004-c23-posix.md`](../specs/004-c23-posix.md)

Seven steps on the single working branch, each green on the three CI cells with `-Dwerror=true`, regress unchanged, the unit
suite unchanged except where the step adds cases.

## Step 0: floor and CI

`meson.build`: `c_std=c23` only; a `cc.compiles` probe with `nullptr`, `constexpr`, a
fixed-type enum and `<stdckdint.h>`; `error()` naming the floor when it fails. `ci.yml`:
gcc-14, clang-20, `macos-26`. `nightly.yml`: clang-20 and clang-tidy-20. `CLAUDE.md`,
`CONTRIBUTING.md`, `README.md`: the floor. Proof: three cells green; an old compiler stops at
`meson setup` with the message.

## Step 1: attributes

Mechanical, no semantic change. `compat.h` loses `__unused`, `__dead`, `__packed`, `__weak`,
`NEED_FUZZING`; `termo.h` loses `printflike`; `xmalloc.h` moves to `[[gnu::format]]`,
`[[gnu::nonnull]]`, `[[nodiscard]]` and drops `__bounded__`. 240 `__unused` to
`[[maybe_unused]]`, 11 `__dead` to `[[noreturn]]`, 8 `/* FALLTHROUGH */` to `[[fallthrough]];`,
2 `__packed` to `[[gnu::packed]]`. `BROKEN___DEAD` and its Meson flag go.
`-Wimplicit-fallthrough` joins `c_args`. Proof: build on the three compilers, full suite,
`grep` for the old macros empty outside `compat/`.

## Step 2: platforms and compat (the POSIX part)

- Delete `osdep-{aix,hpux,sunos,cygwin,haiku,dragonfly,unknown}.c` and their Meson branches;
  `error()` on other hosts. Delete `IS_DARWIN`, `IS_LINUX`, `-D_XPG6` and friends.
- Delete `forkpty-{aix,haiku,hpux,sunos}.c`. Wire the existing `systemd` option to
  `libsystemd` (`HAVE_SYSTEMD`, `ENABLE_CGROUPS`); the code that uses it is intact and is the
  per-pane OOM protection autotools used to give on Linux.
- Delete the shims every target has (`strlcpy`, `strlcat`, `strnlen`, `strndup`, `memmem`,
  `asprintf`, `getline`, `strsep`, `strcasestr`, `cfmakeraw`, `getdtablesize`, `setenv`,
  `clock_gettime`, `err.c`) and their `check_functions` entries.
- Define with a probe every `HAVE_*` a surviving shim reads; delete the rest with their
  branches. First thing in the step: check what `getpeereid.c` returns on Linux today without
  `HAVE_SO_PEERCRED` (socket access control).
- `test_compat.c`: one case per shim that stays, run on macOS and Linux.
Proof: FreeBSD nightly green; the two `HAVE_*` greps (defined-but-unread, read-but-undefined)
both empty.

## Step 3: warnings

Measured 2026-09-07 with clang 21 over 158 translation units: `-Wshadow` 0,
`-Wmissing-prototypes` 0, `-Wstrict-prototypes` 0, `-Wformat=2` 0, `-Wsign-compare` 0,
`-Wvla` 1 (fixed: `mode-tree.c`), `-Wcast-qual` 60, `-Wconversion` 1574 (`sign-conversion`
1239, `shorten-64-to-32` 216, `implicit-int-conversion` 106, float 13). The first six are on
under `-Werror`; the last two stay off and are recorded here. Of the inherited suppressions,
`-Wno-attributes`, `-Wno-unknown-warning-option`, `-Wno-format-y2k` hid nothing and are gone;
`-Wno-deprecated-declarations` hid one call (`daemon` on macOS, fixed by naming the compat one
`termo_daemon`); `-Wno-macro-redefined` (compat `queue.h` against macOS `<sys/queue.h>`),
`-Wno-pointer-sign` (66 `u_char`/`char` sites) and `-Wno-unused-result` (glibc fortify) stay
with their reason in `meson.build`.

## Step 4: checked arithmetic

Read all 25 sites from the inventory. Most are `memcpy`/`memmove` over sizes already
allocated, or `xreallocarray`, which already checks the product; `utf8.c`'s `n + 1` and
`n + size` are bounded by an input string length, `input.c:3491` already has a guard, and
`screen_print` bounds every write against its buffer. Adding `ckd_*` there would be noise.
Done in `grid.c`: `<stdckdint.h>`; `grid_string_grow()` with `ckd_add`/`ckd_mul` replacing the
two duplicated buffer-doubling loops in `grid_string_cells`; `ckd_add` on `hsize + sy + 1` in
`grid_scroll_history`, `grid_scroll_history_region` and `grid_reflow_add`; `new_extdsize` is
`u_int`, not `int`. No `xmallocarray`: `xreallocarray(NULL, n, size)` already is one. Proof:
suite intact, ASAN clean.

## Step 5: typed `termo.h`

Done in `termo.h`, fields left with their original types so no call site changes: the four
`screen_size_x/y`, `screen_hsize/hlimit` macros are `static inline`; the upper-case predicate
macros (`COLOUR_DEFAULT`, `KEYC_IS_*`, `MOUSE_*`, `SCREEN_IS_ALTERNATE`) stay macros, renaming
them would be churn. Eleven flag groups are fixed-type enums with a `static_assert` on their
size: `grid_attr` (`u_short`), `grid_flag` (`u_char`), `grid_line_flag` (`u_short`),
`grid_string_flag`, `screen_mode`, `window_flag`, `winlink_flag`, `pane_flag`, `tty_flag`
(`int`), `format_flag` (`u_int`), `client_flag` (`uint64_t`); composites (`ALL_MODES`,
`CLIENT_ALLREDRAWFLAGS`, ...) are enumerators of the same enum. `constexpr`: `PANE_*`,
`WINDOW_*` limits, `UTF8_SIZE`, `KEYC_NUSER`, `KEYC_CLICK_TIMEOUT`, `MOUSE_PARAM_*`, and the 14
`KEYC_*` modifier, flag and mask constants as `key_code` (the typedef moved above them). The
two anonymous key-code enums are `enum : key_code`, so their > `int` values are well-defined
C23 instead of a compiler extension. The other 99 `0x` defines (`SPAWN_*`, `PROMPT_*`,
`TERM_*`, `CMD_*`, ...) are parameters and small sets without a dedicated field; left alone.
Proof: three compilers, suite intact.

## Step 6: convention and guard

`.clang-tidy` (`bugprone-*`, `cert-*`, `misc-*`, `readability-implicit-bool-conversion`),
nightly clang-tidy fails on new findings over a baseline; `CLAUDE.md` and `CONTRIBUTING.md`
carry the new-code rules.

## Files

Modify: `meson.build`, `meson_options.txt`, `.github/workflows/ci.yml`, `nightly.yml`,
`src/compat/compat.h`, `src/core/termo.h`, `src/core/xmalloc.h`, the files with `__unused`
(densest: `control-notify.c` 28, `window/customize.c` 19, `format.c` 16, `server/client.c`
11, `window/tree.c` 10), the 8 with `FALLTHROUGH`, `grid.c`, `screen.c`, `utf8.c`, `input.c`,
`tests/unit/test_compat.c`, `CLAUDE.md`, `.github/CONTRIBUTING.md`, `README.md`, `ROADMAP.md`.

Delete: 7 `src/osdep/osdep-*.c`, 4 `src/compat/forkpty-*.c`, 14 POSIX-covered shims.

Create: `.clang-tidy`.

## Risks

- Upstream cherry-picks conflict on attributes after Step 1; the same `sed` applies to incoming
  patches. Step 5 confines itself to `termo.h`.
- `macos-26` runner availability; fallback Homebrew `llvm@20`.
- `getpeereid` on Linux may be broken today; Step 2 fixes it first if so.
- `-Wconversion` volume; recorded, not forced.

## Close

All of the above merged, `docs/sdlc/plan/002-luajit.md` (Lua) starts on the modernised tree.

## Progress

Steps 0 to 5 were implemented on 2026-09-07 and the plan never recorded it; verified against the
tree on 2026-09-08, on the Linux gate (gcc-14, clang-20) and macOS:

- Step 0: `c_std=c23`, the probe and its message in `meson.build`; the floor in `README.md`,
  `CONTRIBUTING.md`, `CLAUDE.md`.
- Step 1: `grep -rn '__attribute__\|printflike\|__dead\|__unused'` is empty outside
  `src/compat/`; `-Wimplicit-fallthrough` in `c_args`.
- Step 2: the proof grep needs one correction: the eleven `HAVE_*` that `compat.h` reads
  (`HAVE_CLOSEFROM` ... `HAVE_STRTONUM`) are not spelled out in `meson.build`, the
  `check_functions` loop defines them as `-DHAVE_@0@=1`; with that accounted for, no `HAVE_*` is
  read without a probe and none is probed without a reader. `test_compat.c` has 24 cases, one per
  shim that stays, and runs on both platforms. `base64` is compiled unconditionally since plan 005
  (gcc's libasan breaks glibc's copy).
- Step 3: the six warnings are under `-Werror` in the gate; `-Wconversion` (1574) and
  `-Wcast-qual` (60) recorded above, not forced.
- Step 4: 8 `ckd_*` calls, all in `grid.c`, as decided above; the other inventoried sites stay.
- Step 5: `termo.h` has 25 `constexpr`, 13 fixed-type enums with `static_assert`, 4
  `static inline` size accessors; 19 function-like macros remain, the upper-case predicates the
  step chose to keep.
- Step 6, closed 2026-09-08. `.clang-tidy` runs at zero findings, enforced by the `clang-tidy`
  job on every PR (not a nightly baseline): `WarningsAsErrors: '*'`, the checks it turns off each
  carry a one-line reason, and `src/compat/` (re-imported OpenBSD code) is out of scope. Modelled
  on Neovim, which runs clang-tidy the same way and ports 12k Vim patches by hand, so an inherited
  tree is no reason to keep debt. The run found 668 locations; enabling a check is one commit that
  also fixes it, so the findings were resolved rather than frozen:
  - The 27 `sscanf` parsers (cert-err34-c) are gone. `src/core/scan.c` is a set of cursor scanners
    (`scan_u`/`scan_i`/`scan_x`/`scan_d`/`scan_lit`) that take only digits, bound with `ckd_*`, and
    reject trailing junk; a format is a chain of `&&` ending in `*p == '\0'`. Applied to `colour`,
    `key-string`, `layout`, `refresh-client`, `monitor`, `new-session`, `resize`, control flags and
    the two terminal size replies in `tty/keys.c`; unit module `scan` covers it.
  - signed-char-misuse: `(u_char)` casts in `input/keys.c`, `confirm-before.c`, and `found`/`start`/
    `end` made `u_char` in the copy-mode bracket matcher (`window/copy.c`), the aarch64 char class.
  - `pledge` off OpenBSD went from a `(0)` macro to a `static inline int`, which removes the four
    `misc-redundant-expression` (`(0) != 0`) at its call sites.
  - inc-dec-in-conditions (`arguments.c`, `control.c`, `copy.c`), macro-parentheses
    (`options-table.c`, `termo.h`, `tty.c`), a `static` on a test constant, and the style test's
    struct `memcmp` replaced by a `style_tostring` comparison (padding-independent).
  - Off with a reason in `.clang-tidy`: the tmux idioms (`if ((p=f())==NULL)`, `void **` comparators,
    switch-without-default, branch chains that name each case), widening of bounded integer
    arithmetic, enums with deliberate wire values or sentinels, the declared bidi isolate, the
    key-table string concatenation, `system()` for `lock-command`, terminfo's `(char *)-1`, and
    `readability-implicit-bool-conversion` (noisy in C, Neovim disables it too).
  - `just tidy` runs the exact gate command in the CI image.
