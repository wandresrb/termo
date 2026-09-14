# Research 006: Rust for termo

- Status: Draft 2026-09-09
- Author: wandresrb
- Scope: Rust 1.98 and the 2024 edition, macros, FFI against C23, the memory model and
  fearless concurrency, security and performance tooling, advanced concurrency patterns, the
  crate ecosystem a terminal multiplexer can draw on, how the Rust-based terminals and
  multiplexers are built, the roles of C, Lua and Rust in one process, and the Neovim 0.12
  patterns the extension layer should mirror. External research only; nothing here comes
  from reading the termo tree. tmux facts are its public design.

## 1. Rust in September 2026

Stable: 1.98.0 (2026-08-20). 1.97.0 (2026-07-09) got a 1.97.1 a week later for an LLVM
miscompilation. Edition 2024 shipped in 1.85 (2025-02-20). Six-week cadence.

### 1.1 Edition 2024, the parts that matter for systems code

- `unsafe extern { }`: every foreign block is explicitly unsafe; each item inside can be
  marked `safe fn` when the declaration itself carries no precondition.
- `#[unsafe(no_mangle)]`, `#[unsafe(export_name)]`, `#[unsafe(link_section)]`: exporting a
  symbol is an unsafe act (symbol clashes, ABI).
- `unsafe_op_in_unsafe_fn` warns by default: an `unsafe fn` body no longer is one big unsafe
  block; each operation gets its own `unsafe { }` and its own `// SAFETY:` line.
- References to `static mut` are a deny-by-default error; the replacements are
  `&raw mut STATIC` (1.82), `SyncUnsafeCell`, atomics, `LazyLock`, or a `Mutex`.
- RPIT captures all in-scope lifetimes by default; `impl Trait + use<'a, T>` opts out.
- `if let` and tail-expression temporaries drop earlier; never-type fallback to `!`;
  `gen` reserved; `expr` fragment matches `const {}` and `_`.
- Cargo: MSRV-aware resolver, `unused` inherited default-features rejected; rustfmt style
  edition 2024.

### 1.2 Stabilisations since 1.80 that a C23 engineer will reach for

| Since | Feature | Why it matters here |
|---|---|---|
| 1.80 | `LazyLock`/`LazyCell`; `$count`, `$index`, `$len`, `$ignore` metavariable expressions | lazily built tables without `static mut`; repetition counting in `macro_rules!` |
| 1.81 | `extern "C-unwind"`; a panic escaping a plain `extern "C"` fn aborts; `#[expect(lint)]`; `core::error` | the FFI contract: `extern "C"` = abort on panic, never UB |
| 1.82 | `unsafe extern`, `&raw const/mut`, `use<..>` precise capture, `Option::is_none_or`, safe `#[no_sanitize]`-free patterns | raw pointers to packed fields without creating references |
| 1.83 | `const` fn with `&mut`, `Option::take_if`, `Vec::pop_if` | compile-time tables |
| 1.84 | `Ipv*` const, strict provenance APIs (`addr`, `with_addr`, `expose_provenance`) | pointer tagging done legally |
| 1.85 | edition 2024, async closures, `#[diagnostic::do_not_recommend]`, `FromIterator for Box<str>` | |
| 1.86 | trait upcasting, safe `#[target_feature]` fns, `get_disjoint_mut`, `Vec::pop_if` | SIMD paths behind runtime detection callable from safe code |
| 1.87 | `std::io::pipe`, `Vec::extract_if`, `String::extend_from_within`, `asm_goto`, `use<..>` in trait defs | a wakeup pipe from a worker into a C event loop |
| 1.88 | `let` chains (2024), naked fns `#[unsafe(naked)]`, `cfg(true)/cfg(false)`, `Cell::update`, `-Cdwarf-version` | parser dispatch reads better with chains |
| 1.89 | `i128`/`u128` FFI-safe on x86_64, `#[repr(u128)]`, AVX-512 target features and intrinsics, `NonNull::from_ref/from_mut`, `File::lock` | |
| 1.90 | `lld` default linker on x86_64-linux-gnu; volatile access to address 0 allowed | link time |
| 1.91 | C-variadic fn definitions for `sysv64`/`win64`/`efiapi`/`aapcs`; thread id in panic messages | |
| 1.92 | `MaybeUninit` layout documented; `&raw` union fields in safe code; `Box::new_zeroed`, `Arc::new_zeroed_slice`; `RwLockWriteGuard::downgrade` | zeroed allocations without a `memset` pass |
| 1.93 | `[MaybeUninit<T>]::assume_init_*`, `write_copy_of_slice`; `Vec::into_raw_parts`, `String::into_raw_parts`; `[T]::as_array`; `char::MAX_LEN_UTF8`; `-Cjump-tables=bool`; C-variadic `system` ABI | handing buffers across FFI without a copy; uninitialised line buffers |
| 1.94 | `[T]::array_windows`, `element_offset`, `LazyLock::get/force_mut`, Unicode 17 | in-tree width tables at Unicode 17 while host `wcwidth` lags |
| 1.95 | `if let` guards, `cfg_select!`, `core::hint::cold_path`, `core::range` (new `Range` types), `Atomic*::update/try_update`, `Layout::repeat/extend_packed`, `<*const T>::as_ref_unchecked`, `Vec::push_mut` | `cold_path` on parser error arms; `Layout` for packed cell arrays |
| 1.96 | `assert_matches!`, `From<T> for LazyLock`, `expr` metavars in `cfg`, `ManuallyDrop` constants as patterns | |
| 1.97 | v0 symbol mangling by default; cargo `build.warnings` (the `-Dwarnings` you can put in config); `bit_width`, `isolate_highest_one`; linker output no longer hidden | sanitizer and `perf` stacks show v0 names; Meson/Cargo CI can deny warnings by config |
| 1.98 | `str::substr_range`, `[T]::subslice_range`, `NumBuffer`/`format_into` (allocation-free integer formatting), `Atomic<T>::from_mut_slice`, algebraic float ops, `String::from_utf16le/be` | match offsets without pointer arithmetic; formatting replies with no `String` |

### 1.3 Still unstable, so not in a plan that targets stable

`std::simd` (portable SIMD: `std::arch` intrinsics and crates such as `memchr`, `wide`,
`pulp` are the stable route); `std::sync::mpmc` (tracking #126840; `std::sync::mpsc` is
the crossbeam algorithm since 1.67 and is what to use); sanitizers as `-Csanitize`
(§4.5); `likely`/`unlikely` (likely to be dropped in favour of the stabilised `cold_path`);
macros 2.0 (`macro` items); `#[no_sanitize]`; `try` blocks; specialization; generic
`Atomic<T>` beyond the conversions; contracts.

## 2. Macros

### 2.1 Declarative (`macro_rules!`)

Hygienic, pattern-based, expanded before type checking, no build-time cost worth measuring.
Since 1.80 the metavariable expressions `${count(x)}`, `${index()}`, `${len()}` and
`${ignore(x)}` are stable, which removes the classic "count the repetitions with a
recursive helper" trick. `cfg_select!` (1.95) is the `match` over `cfg` predicates. Fragment
specifiers to know: `expr` (2024 edition also matches `const {}` and `_`), `expr_2021`,
`pat`, `ty`, `ident`, `literal`, `tt`, `block`, `item`, `meta`, `vis`, `path`. `$crate` for
paths, `#[macro_export]` for cross-crate, `pub(crate) use` re-exports for module-scoped
macros (the modern pattern instead of `#[macro_use]`).

Where they pay in a terminal core: generating dispatch tables (CSI final byte × handler),
packed-struct size assertions (`const _: () = assert!(size_of::<T>() == N)`), enumerating
the option table once and deriving name/type/default/doc from it, and the
`#[repr(C)]`-mirror boilerplate for FFI structs. The Little Book of Rust Macros is the
reference for the shape rules.

### 2.2 Procedural

Three kinds: `#[derive(X)]`, attribute `#[x]`, function-like `x!()`. They are separate
crates of `proc-macro = true`, run in the compiler, and cost compile time proportional to
`syn` (v2) parsing. `proc_macro::Span` gained `line`/`column`/`start`/`end` (1.88) and
`Ident::new` accepts `$crate` (1.90). `#[diagnostic::on_unimplemented]` (1.78) and
`#[diagnostic::do_not_recommend]` (1.85) are what a library uses to make trait errors
readable without a proc macro.

When to write one: only when the input is not expressible as token trees a `macro_rules!`
can match, or when the output must inspect a type's fields (a derive). For a C-shaped
engine that is two cases: deriving the C-ABI mirror + size check for a struct, and
deriving a `Display`/`FromStr` pair for option enums. Everything else stays declarative.
`derive` itself is now nameable as `core::derive` (1.96, formalised 1.98).

## 3. FFI with a C23 core

### 3.1 The layering that works (Linux kernel model)

The kernel's `Documentation/rust/general-information` is the reference for a large C
codebase adding Rust: a `bindings` crate generated by bindgen from the C headers that no
one else touches; a `kernel` crate of safe abstractions that "encapsulate the unsafe access
to the bindings into an as-safe-as-possible API"; leaf code that "should not use the C
bindings directly"; `static inline` functions and macros reached through small C helper
wrappers (`rust/helpers/`). Every `unsafe` block carries a `SAFETY:` comment naming the
contract it relies on.

### 3.2 Tools

- **bindgen** 0.73.2 (2026-09-08): C header → Rust `extern` declarations. `--wrap-static-fns`
  (stable since 0.71) emits a C file of wrappers for `static inline` functions so they are
  callable; libclang 20 parses C23, but the changelog has no C23-specific entries, so
  `constexpr` objects and `nullptr` need a check on the first run. Distro packages lag
  (Ubuntu 24.04 ships 0.66); install from `cargo install bindgen-cli`.
- **cbindgen**: Rust `extern "C"` items → C header; the ABI is written once, in Rust, and
  C includes the generated header. Handles `#[repr(C)]` structs, enums with fixed repr,
  opaque types, `Option<extern "C" fn>` as nullable function pointers.
- **Build without cargo**: Meson has native Rust targets (`static_library(..., rust_abi:
  'c')` produces a `staticlib`; `rust.bindgen()` with `output_inline_wrapper` since 1.3;
  `rust.cbindgen()` since 1.12; `rust.test()`/`rust.doctest()`; mixed C/Rust targets since
  1.9 with rustc as linker; Cargo workspaces and subprojects since 1.11, still marked
  experimental). Meson 1.11 also wraps `link_args` as `-Clink-arg=`.
- **Build with cargo**: `crate-type = ["staticlib"]`, `panic = "abort"`, `lto = "fat"`,
  `codegen-units = 1` in the release profile, `cargo build` from the outer build system,
  link `libtermo_rs.a` plus `-lpthread -ldl -lm` (`cargo rustc --print native-static-libs`
  lists them). This is what GStreamer, librsvg, Firefox and the kernel-adjacent projects
  did before Meson's Rust support matured.
- **Symbols**: v0 mangling is the default since 1.97; exported C symbols are unaffected
  (`#[unsafe(no_mangle)]`/`export_name`), but sanitizer and `perf` stacks of internal Rust
  frames show `_R…` names that `rustfilt`/`c++filt` demangle.

### 3.3 Boundary rules

1. Shared structs: `#[repr(C)]` (or `#[repr(C, packed)]` where C packs), fixed-repr enums
   (`#[repr(u8)]`), `static_assert` on the C side and `const _: () = assert!(size_of / 
   align_of)` on the Rust side. Never take a `&` to a field of a packed struct; use
   `&raw const` and `ptr::read_unaligned`.
2. Functions exported to C are `pub extern "C" fn` with `#[unsafe(no_mangle)]`; they take
   `(*const u8, usize)` pairs and build one slice at the edge with an explicit "valid for
   `len` bytes" contract; they never take `&T` to memory C may mutate concurrently
   (`UnsafeCell`/raw pointers instead).
3. Panics: since 1.81 a panic unwinding out of `extern "C"` aborts, which is the C
   `abort()` semantics C callers already live with; `extern "C-unwind"` is for the rare
   case where the C side is compiled with unwind tables and wants to catch. In a static
   library linked into a C executable set `panic = "abort"` to drop the unwinder and
   landing pads.
4. Allocation: use the system allocator so the C side's sanitizer and `malloc_trim` see
   it; hand buffers to C with `Vec::into_raw_parts` (1.93) and take them back with
   `from_raw_parts` with the same capacity; free on the side that allocated, always.
5. Strings: `CStr`/`CString` at the edge only; `c"literal"` (1.77) for constant C strings;
   `CStr::count_bytes` (1.79); `core::ffi::c_char`, `c_int`, `c_void` from `core::ffi`
   (no `libc` dependency needed for the types).
6. Callbacks: C function pointers as `Option<unsafe extern "C" fn(...)>` (nullable), the
   `void *user` pointer carried as `*mut c_void`; a Rust closure crossing into C is boxed
   and its pointer handed as the user data, with an explicit drop function exported.
7. Errors: integer codes or a `#[repr(C)]` result struct at the edge; `Result` inside.
8. Lints at the crate root: `#![deny(unsafe_op_in_unsafe_fn, clippy::undocumented_unsafe_
   blocks, clippy::arithmetic_side_effects, clippy::cast_possible_truncation,
   improper_ctypes, improper_ctypes_definitions)]`. `improper_ctypes` is the compiler
   telling you a type is not FFI-safe.

### 3.4 C23 ↔ Rust correspondence

| C23 | Rust |
|---|---|
| `nullptr` / nullable pointer | `Option<NonNull<T>>` (same size, null = `None`) |
| `bool` | `bool` (ABI-compatible, 1 byte) |
| fixed-type `enum : uint8_t` | `#[repr(u8)] enum`; only if every C value is a variant, else a newtype `struct Flags(u8)` with `bitflags`-style consts |
| `constexpr` object | `const` |
| `static_assert` | `const _: () = assert!(...)` |
| `[[nodiscard]]` | `#[must_use]` |
| `[[noreturn]]` | `-> !` |
| `[[maybe_unused]]` | `#[allow(unused)]` / `_name` |
| `[[fallthrough]]` | no fallthrough; `match` with `|` patterns |
| `[[gnu::format(printf)]]` | `format_args!` / `impl Display`; variadics only via `extern "C"` declarations |
| `ckd_add/ckd_mul` | `checked_add`, `overflowing_*`, `strict_*`; `arithmetic_side_effects` deny makes plain `+` on `usize` a lint |
| `_Generic` | traits and generics |
| `typeof` | inference |
| `restrict` | `&mut` (exclusive by construction; stronger than `restrict`) |
| `__attribute__((cleanup))` | `Drop` |
| `_BitInt(N)` | no direct type; `u128`/arrays; not FFI-safe across the boundary |
| `alignas` | `#[repr(align(N))]` |
| `#embed` | `include_bytes!` |
| `char8_t` / `u8"..."` | `&[u8]` / `&str` (UTF-8 guaranteed) |
| `memset_explicit` | `zeroize` crate or `ptr::write_volatile` loop |

### 3.5 Sanitizers across the boundary

rustc's sanitizer support is `-Zsanitizer=address|thread|memory|leak|cfi|kcfi|hwaddress`
(nightly). Stabilisation is a 2025H2/2026 project goal: ASan and LSan are being stabilised
first as `-Csanitize`, with Tier 2 targets that ship a pre-instrumented std
(`x86_64-unknown-linux-gnuasan`, `-gnutsan`, `-gnusan`) so users do not need
`-Zbuild-std`. Practical rules today:

- rustc links its ASan runtime statically; a process that also loads GCC's dynamic
  `libasan.so` fails at start with "incompatible ASan runtimes". With GCC on the C side
  use `-static-libasan`; clang statically links by default.
- Uninstrumented Rust linked into an instrumented C binary is sound: ASan still intercepts
  `malloc`/`free` from Rust's system allocator (double free, leak, use-after-free of heap
  memory are caught) but misses overflows inside Rust `unsafe` code. That gap is Miri's
  job on the Rust unit tests.
- ThreadSanitizer needs an instrumented std (`-Zbuild-std` or the `gnutsan` target); for
  Rust-only concurrent code `loom` is the practical tool (§4.4).

## 4. Fearless concurrency and the memory model

### 4.1 The guarantees

- Ownership plus `Send` (a value may move to another thread) and `Sync` (`&T` may be shared
  across threads) turn "which state crosses threads" into a compile error. Raw pointers and
  `Rc`/`RefCell` are neither; a type holding a `*mut c_struct` is `!Send` until you assert
  otherwise with `unsafe impl Send`, and that assertion is the whole audit surface.
- Atomics follow the C++20 memory model (`Ordering::{Relaxed, Acquire, Release, AcqRel,
  SeqCst}`, fences, `compiler_fence`). No `volatile` for synchronisation; `read_volatile`
  is for MMIO.
- Aliasing of raw pointers is governed by the operational models Miri implements: Stacked
  Borrows and, since 2025, Tree Borrows (PLDI 2025), which accepts more real-world unsafe
  code (54% fewer rejections across crates.io in the paper's evaluation) while still
  enabling the `&mut` noalias optimisations. Miri (POPL 2026 paper) is the only tool that
  checks code against it; BorrowSanitizer is the in-progress LLVM-level equivalent for
  binaries.
- `std::thread::scope` (1.63): borrowed data in workers, joined before the scope ends, no
  `Arc` and no `'static`.
- `Mutex` poisoning on panic; `parking_lot` for smaller, faster, non-poisoning locks and
  fair variants; `std::sync::mpsc` is crossbeam's algorithm (1.67); `RwLockWriteGuard::
  downgrade` (1.92); `Atomic*::update` (1.95).

### 4.2 Patterns that fit a one-process, one-event-loop server

A tmux-style server is one process, one libevent loop, one thread; every object (session,
window, pane, grid, client tty) is loop-thread-only, and libevent itself is not
thread-safe unless `evthread_use_pthreads` is enabled. Adding Rust does not change that
constraint; it makes the safe ways to work around it explicit types.

1. **Parse off-thread, apply on-thread.** A worker owns the pty read and the VT parser and
   emits a batch of typed actions (`Print(str)`, `Csi{..}`, `Osc{..}`, `Dcs{..}`) over a
   channel; the loop thread applies them to the grid in order. WezTerm does exactly this:
   `read_from_pane_pty` on a thread per pane feeds a `parse_buffered_data` thread using
   `termwiz::escape::parser::Parser`, with a coalescing delay and a 1 MiB buffer, and
   applies actions to a `Mutex<Terminal>`, notifying the mux with
   `MuxNotification::PaneOutput`. Alacritty's `alacritty_terminal` runs one "PTY reader"
   thread that parses with `vte` straight into `Arc<FairMutex<Term>>`, holding the lock for
   at most `MAX_LOCKED_READ = 65535` bytes per acquisition and waking the renderer with
   `Event::Wakeup`. The wakeup into a C event loop is a pipe or `eventfd` the loop
   watches (`std::io::pipe`, 1.87).
2. **Single writer, many readers by snapshot.** Readers (redraw, search, a control-mode
   client) take an immutable snapshot; the writer swaps it. `arc-swap` (lock-free loads,
   wait-free most of the time), `left-right` (two copies, oplog replay, reads never
   block), or a generation counter + `RwLock` when snapshot cost is acceptable. Fits a grid
   whose readers outnumber its one writer, at the price of copy-on-write for the mutated
   lines only.
3. **Scoped data parallelism** for read-only bulk work: `std::thread::scope` or `rayon`
   over line ranges (scrollback search, reflow of a frozen grid, width recomputation).
   Sound when the data is provably not being written (a private copy, a snapshot, a resize
   window).
4. **Actor per pane** (channels + owned state, `tokio`/`smol`/`std` threads): what Zellij
   does across its `screen`, `pty` and `plugin` threads with message enums; it moves the
   serial point to "one actor per pane" and the fan-out to N clients stays serial. The
   cost is that every cross-pane operation is a message exchange.
5. **Thread-per-core with io_uring** (`glommio`, `monoio`, `compio`, `tokio-uring`):
   proven at 1.1M+ req/s in 2026 benchmarks, Linux-only for three of the four, and with a
   real cancellation-safety hazard in async Rust ("async Rust is not safe with io_uring"
   unless buffers are owned by the ring). Not the shape of a multiplexer whose bottleneck
   is a single serial redraw, and it collides with libevent owning the fds.

The honest summary from the Rust terminals: none of them removed the serial section; they
moved it from "one thread" to "one mutex around the terminal state". Ghostty's SIMD work
(Zig) is the datapoint on where the time goes: 7.3x on ASCII throughput and 16.6x on UTF-8
decode in the parser stage, but 2x on a real `cat` of ASCII and 20% on Japanese text
because the rest of the pipeline is serial, and codepoint-width lookup alone was 30% of the
per-character cost before a lookup table fixed it (2.8x). Amdahl applies before any thread
is spawned.

### 4.3 Channels and lock-free structures, stable choices

`std::sync::mpsc` (bounded/unbounded, crossbeam algorithm), `crossbeam-channel` (select,
mpmc), `flume` (mpmc, async-capable), `crossbeam-epoch`/`crossbeam-deque` (work stealing),
`arc-swap`, `left-right`, `dashmap`, `slab`/`slotmap` (index-stable arenas that replace
intrusive `TAILQ`/`RB_HEAD` ownership: handles instead of pointers, no dangling).

### 4.4 Verification and testing of concurrent and unsafe code

- **Miri**: interprets MIR, checks UB (aliasing under Tree Borrows, uninit reads, data
  races with its weak-memory emulation, provenance); runs the crate's `#[test]`s; cannot
  cross FFI, so the Rust side needs tests that do not call C.
- **loom**: exhaustive interleaving of a model with `loom::sync` types; the standard tool
  for lock-free code; test-only dependency.
- **Kani**: bit-precise model checker (CBMC over MIR) with function and loop contracts;
  runs 16,000+ harnesses per change on the std verification campaign (2026 paper); best on
  small unsafe kernels (a packed-cell codec, a UTF-8 decoder), not whole programs.
- **Sanitizers**: §3.5.
- **Fuzzing**: `cargo-fuzz` + `libfuzzer-sys` (needs nightly for `-Zsanitizer`; the
  coverage flags `-Cpasses=sancov-module -Cllvm-args=-sanitizer-coverage-*` themselves are
  stable), `afl.rs`, `arbitrary` for structured inputs, `proptest`/`quickcheck` for
  property tests. Differential fuzzing (C and Rust implementations fed the same bytes,
  outputs compared) is the right oracle for a strangler-fig port.

## 5. Security

### 5.1 What Rust removes, and what it does not

Removed in safe code: spatial errors (stack/heap overflow, out-of-bounds index → panic),
temporal errors (use-after-free, double free), uninitialised reads, data races, integer
overflow as silent wraparound (panic in debug, defined wrap in release unless `checked_`/
`strict_`/`overflowing_` are used; `arithmetic_side_effects` as deny forces the choice).
Kept: logic bugs, resource exhaustion (an unbounded buffer is unbounded in any language),
injection through legitimate channels (a title or hyperlink string that reaches a shell),
policy (clipboard read-back, passthrough sequences), and everything inside `unsafe`.

The terminal-emulator CVE class illustrates both halves. tmux's one real CVE,
CVE-2020-27347, was a stack buffer overflow in the SGR colon-parameter parser (`input.c`,
fixed in 3.1c): spatial, removed by a bounds-checked `[i32; 8]`. CVE-2022-47016 was
withdrawn by the CNA as not a security issue. The other historic terminal bugs (title
report echo, OSC 52 clipboard read-back, DCS passthrough, hyperlink URI injection, sixel
allocation size) are policy and limits, unaffected by the language. The WebAssembly route
Zellij took for plugins carries its own runtime's CVEs (Wasmtime's April 2026 advisories,
CVE-2026-47261 preopen bypass, CVE-2026-54786 fd exhaustion), which is the argument for not
embedding one.

### 5.2 Tooling

- `clippy` with `pedantic` and the unsafe-related lints as deny;
  `#![forbid(unsafe_code)]` in every crate that has no reason to hold unsafe (only the
  bindings and abstraction layers hold it).
- `cargo-audit` (RUSTSEC advisories against `Cargo.lock`), `cargo-deny` (licences,
  duplicates, sources, advisories as CI policy), `cargo-vet` (audit trail per crate and
  version: Mozilla/Google's supply-chain answer; works as a text file even without
  cargo in the build), `cargo-geiger` (count of unsafe in the dependency tree).
- Miri, Kani, sanitizers, fuzzing: §4.4. `cargo-careful` (debug assertions inside std)
  is subsumed by Miri where Miri runs.
- Reproducible builds: `--remap-path-scope` (1.95) and `-Zbuild-std`-free Tier 2
  targets keep binaries deterministic for a SARIF/Scorecard pipeline.

## 6. Performance

### 6.1 Zero-cost abstractions, concretely

Monomorphised generics and inlined closures compile to the loop you would write in C;
`Iterator` chains vectorise when bounds are provable; `enum` dispatch on a `#[repr(u8)]`
enum is a jump table (`-Cjump-tables=bool`, 1.93, to test the alternative); `Option<NonNull>`
and `Option<&T>` are one pointer; `#[inline]` is a hint, `#[inline(always)]` is a
demand, cross-crate inlining needs one of them or LTO; `#[cold]` and `core::hint::cold_path`
(1.95) shape branch layout the way `__builtin_expect` does. Bounds checks are the one cost
that C does not pay; they are hoisted by LLVM when the index is derived from `len`, and
they are what turns CVE-2020-27347 into a panic.

### 6.2 Build configuration (Rust Performance Book)

`opt-level = 3`, `lto = "fat"` (10–20% typical, whole-program inlining across crates),
`codegen-units = 1`, `panic = "abort"`, `-Ctarget-cpu=native` only for local builds
(`x86-64-v3` for distribution), `debug = "line-tables-only"` to keep `perf` usable.
PGO adds 5–15% on top of a release build and BOLT re-lays out the binary for i-cache;
`cargo-pgo` wraps both (instrument → run a representative workload → optimise). For a
multiplexer the representative workload is a VT benchmark run through a pty (`vtebench`
from the Alacritty project, or `cmuratori/termbench`), which is also the regression gate.

### 6.3 SIMD on stable

`std::arch` intrinsics under `#[target_feature(enable = "...")]` with
`is_x86_feature_detected!`/`is_aarch64_feature_detected!` for runtime dispatch (safe to
call since 1.86); `memchr` (SIMD search for one to three bytes, the ESC/0x80 scan a VT
parser's ground state wants), `wide`/`pulp`/`simdutf8` (UTF-8 validation at
memcpy speed). `std::simd` is nightly only. Ghostty's numbers (§4.2) are what to expect
from this stage in isolation.

### 6.4 Measuring

`criterion` and `divan` for microbenchmarks; `iai-callgrind` for instruction counts
(stable in CI, immune to noise); `perf` + `samply`/`flamegraph`/`hotspot` for profiles;
`dhat` for allocation profiles; `cargo-show-asm` to read the generated code of one
function. Allocators: `mimalloc` or `jemallocator` as the global allocator are the
cheapest 5–20% on allocation-heavy paths, but they hide allocations from a C-side
sanitizer and `malloc_trim`; keep the system allocator while sanitizers matter.

## 7. The ecosystem a terminal multiplexer can use

| Area | Crate | Notes |
|---|---|---|
| VT parsing | `vte` 0.15 (Alacritty; Paul Williams state machine, `Perform` trait, allocation-free) | the reference implementation to compare a port against; `vte::ansi` has the full CSI/OSC/DCS action set |
| VT parsing + terminal model | `termwiz` (WezTerm: `escape::parser::Parser` producing typed `Action`s, cell/line/surface model, Unicode width tables), `wezterm-term`, `alacritty_terminal` | complete terminals as libraries; too opinionated to embed under a tmux grid, invaluable as an oracle |
| pty | `portable-pty` (WezTerm), `rustix-openpty`, `nix` | |
| syscalls | `rustix` (safe, no libc types), `libc` (raw), `nix` | |
| event loop | `mio`, `polling`, `calloop`; io_uring runtimes §4.2 | irrelevant while libevent owns the loop; relevant if a Rust worker needs its own poll |
| Unicode | `unicode-width` (Unicode 17 with 1.94-era tables; the crate tmux-style width logic maps onto), `unicode-segmentation` (graphemes), `icu_segmenter`/`icu_normalizer` (ICU4X, heavy, correct), `simdutf8` | width tables in the tree instead of the host `wcwidth` is the single biggest Unicode-correctness lever |
| regex | `regex` (linear time, no backreferences, no look-around, leftmost-first not POSIX leftmost-longest), `regex-lite`, `fancy-regex` (backrefs, backtracking) | a POSIX `regcomp` user switching to `regex` changes alternation semantics; must be a documented decision |
| strings/buffers | `memchr`, `bytes`, `smallvec`, `arrayvec`, `compact_str`, `bstr` (byte strings that may not be UTF-8: exactly what pty output is) | |
| arenas | `slab`, `slotmap`, `bumpalo` | handles instead of intrusive pointers |
| concurrency | `crossbeam`, `rayon`, `parking_lot`, `arc-swap`, `left-right`, `flume` | §4 |
| Lua | `mlua` 0.12 (5.1–5.5, LuaJIT, Luau; `module` feature builds a `cdylib` loadable with `require`; `send`, `async`, `serde` features; MSRV 1.88, MIT) | `Lua::init_from_ptr` attaches to an existing `lua_State` created by C (unsafe, documented); WezTerm's whole config layer is mlua |
| serialisation | `serde` + `serde_json`, `rmp-serde` (msgpack, Neovim's RPC), `postcard` | control-mode / RPC codecs |
| CLI/config | `clap`, `toml`, `kdl` | |
| observability | `tracing`, `tracing-subscriber` | |
| tests | `proptest`, `arbitrary`, `insta` (snapshots), `criterion`, `divan`, `iai-callgrind`, `loom`, `kani` | |
| plugins ABI | `abi_stable`, `stabby` (stable Rust-to-Rust ABI for dylibs), `libloading` | only if a Rust-native plugin ABI is ever wanted; the C ABI through LuaJIT `ffi` needs none of them |
| TUI | `ratatui`, `crossterm` | for external tools talking to the multiplexer, not for the core |

Rules of thumb for vendoring without cargo: prefer crates with zero dependencies
(`memchr`, `unicode-width`, `libc`, `smallvec`, `arrayvec`, `slab`, `arc-swap`); treat
`regex` as a 40k-line subsystem (`regex-automata`, `regex-syntax`, `aho-corasick`,
`memchr`) and `mlua` as a runtime decision, not a utility.

## 8. How the Rust terminals and multiplexers are built

| Project | Language | Threading | VT parser | Extension model | Lesson |
|---|---|---|---|---|---|
| tmux | C, libevent | one server thread; client/server over a Unix socket; one process owns every session | hand-written table state machine | commands, hooks, formats, control mode; `run-shell` fork+exec | the model termo keeps: serial, small, predictable |
| WezTerm | Rust (19+ crates) | reader thread + parser thread per pane; `Mutex<Terminal>`; mux notifications; separate GUI and `wezterm-mux-server` | `termwiz` | Lua config through `mlua`; plugins are Lua-only git repos (`wezterm.plugin.require`), no versioning, `update_all()` manual | Rust core + Lua surface works; plugins do not need native code |
| Alacritty | Rust | one PTY reader thread per window, `FairMutex<Term>`, 64 KiB max per lock hold, renderer thread | `vte` | none (config file) | the simplest sound threading of a terminal |
| Zellij | Rust | multi-threaded (`screen`, `pty`, `plugin`, route) with message enums over channels | `vte` | WebAssembly/WASI plugins (Rust officially, others via community), permission system, protobuf messages | the plugin toolchain cost is the recurring complaint; the runtime carries CVEs |
| Ghostty | Zig | app-driven; SIMD VT stage | own, table-driven, SIMD ground state | `libghostty-vt` C API "for testing soon", 1.0 within six months; Zig module usable now | where the parser-stage time actually goes (§4.2); a C-ABI VT library is coming |
| Neovim | C + LuaJIT + libuv | one main loop; `vim.schedule`/`vim.uv` for async; msgpack-RPC for remote UIs and plugins | n/a | Lua in-process, remote plugins over RPC, `nvim-oxi` for Rust cdylib plugins via the C API | the pattern termo already follows |
| Helix | Rust | one loop (tokio) | n/a | Steel (Scheme) plugin system, PR #8675, still unmerged in 2026 | a Rust core does not make the plugin question easier |
| Zed | Rust | GPUI | n/a | WebAssembly extensions | same trade as Zellij at editor scale |

Hypotheses this supports for a tmux-shaped core adding Rust:

- H1, strangler-fig leaves behind the existing C ABI: parser, UTF-8, grid storage. Zero
  change to the object graph, differential fuzz as the oracle, measurable per module.
- H2, parse off-thread once H1's parser exists: the WezTerm/Alacritty shape, with the apply
  step on the loop thread. Worth it only if a benchmark shows the parser stage bounds
  throughput (Ghostty's data says the apply/draw stage usually does).
- H3, snapshot-based readers for search and control-mode consumers (`arc-swap`/
  `left-right`), the one concurrency pattern that needs no lock on the writer's path.
- H4, greenfield subsystems in Rust with no C twin (a msgpack/JSON RPC codec, a scrollback
  index, a width table): no port cost, no cherry-pick divergence, full crate use.
- H5, not a hypothesis to test: a WASM runtime, a Go/JS runtime, or a Rust rewrite of the
  command/session graph.

## 9. Roles: C, Lua, Rust, and how they connect at zero cost

| | Owns | Connection |
|---|---|---|
| C23 | the engine: server, clients, command pipeline, layouts, modes, tty drawing, options, formats, events; the part that keeps receiving upstream fixes | calls Rust through a generated header (cbindgen), links a `staticlib`; `static inline` helpers wrapped by bindgen |
| Rust | leaf modules that parse untrusted bytes or run hot loops (VT parser, UTF-8, grid storage, sixel, search), plus greenfield subsystems | exports `extern "C"` behind `#[repr(C)]` types; never touches the C object graph except through the safe abstraction crate |
| LuaJIT | configuration, keymaps, UI composition, package management, plugins: the user's language, in-process, budgeted with `lua_sethook`, fault-isolated with `pcall` | the C API (`lua_CFunction` tables) for the core; `ffi` for native plugins |

Rust as a plugin language has three routes and a precedent for each:

1. **LuaJIT `ffi` → Rust `cdylib`** (Neovim's `nvim-oxi` shape, with plain C signatures
   instead of typed bindings). `ffi.load("plugin.so")` plus `ffi.cdef` of the
   `#[unsafe(no_mangle)] extern "C"` functions; zero core changes; the plugin's ABI is the
   C ABI it declares; a panic aborts the host unless caught inside; time inside the C call
   is invisible to a Lua instruction-count budget. `mlua` with the `module` feature builds
   the same artifact with a typed API and `require`-able entry point, at the cost of a
   second Lua binding layer in the process.
2. **Out of process over RPC** (Neovim remote plugins, msgpack-RPC; WezTerm's mux codec):
   any language, process isolation, socket latency per call.
3. **WASM** (Zellij, Zed): sandboxing at the cost of a runtime, its CVEs, serialised IPC and
   a compiler toolchain to add a keybinding.

WezTerm shows the equilibrium for a multiplexer: Lua for everything users write, no
native plugin story, and the core in Rust. Helix shows the failure mode of skipping Lua.

## 10. Neovim 0.12 patterns (released 2026-03-29)

- **`vim.pack`**, the built-in package manager: `vim.pack.add({ { src, name, version,
  data } })` where `version` is a branch, tag, commit or `vim.version.range('2.x')`;
  plugins live in `pack/core/opt/` under the data dir; a lockfile
  (`nvim-pack-lock.json`) in the config dir reproduces a machine and reverts updates;
  `update()` opens a confirmation buffer (with an embedded LSP server for hover/actions)
  before pulling; `del()`; `PackChangedPre`/`PackChanged` autocommands with the change
  kind; Git only; every plugin is "opt" so commenting out a line removes it; lazy loading
  by `vim.schedule` or an autocommand, used "in moderation". Design principle: adding a
  plugin is a function call, like a mapping or an autocommand, not a framework.
- **Builtins over plugins**: `vim.lsp.config`/`vim.lsp.enable` (declarative server
  config, no lspconfig), native insert-mode `'autocomplete'`, default LSP mappings
  (`grn`, `gra`, `gri`, `grr`), a default statusline with diagnostics and LSP progress,
  tree-sitter markdown highlighting and node text objects (`an`/`in`/`]n`/`[n`) by
  default, `ui2` (experimental UI that removes "Press ENTER" prompts).
- **Lua API surface**: `vim.api.*` (typed, documented, RPC-mirrored), `vim.keymap.set`,
  `vim.opt`/`vim.o`, `vim.api.nvim_create_user_command`, autocommands as the event bus,
  `vim.ui.select/input` (overridable UI hooks), `vim.system` (async processes),
  `vim.net.request` (HTTP, new in 0.12), `vim.fs`, `vim.text` (`vim.diff` moved to
  `vim.text.diff`), `vim.list.unique/bisect`, `vim.uv` (libuv: timers, fs, pipes) and
  `vim.schedule` for "run on the main loop when safe" (the same rule as a callback that
  must not re-enter the core mid-command).
- **Runtime conventions**: `runtimepath` with `plugin/`, `lua/`, `after/`, `ftplugin/`,
  `doc/`; `:checkhealth` as the diagnostic entry point every plugin can extend; `:restart`
  and `:connect` (0.12) for the server/client model; `nvim --embed`/`--listen` and
  msgpack-RPC for external UIs and agents.
- **Modes and motions**: Normal/Insert/Visual/Operator-pending/Command-line, text objects,
  operators composing with motions, `vim.on_key`, modal key tables with `which-key`-style
  hints being the community layer on top. For a multiplexer the analogues are copy mode
  (motions, text objects, search), modal key tables with a hint bar, and a command
  palette; tree-sitter itself has no role in a scrollback beyond prompt marks (OSC 133).
- **Rust in the Neovim world**: none in core; `nvim-oxi` 0.6 gives Rust plugin authors
  typed bindings to the C API from a `cdylib` loaded with `require`, advertised for "access
  to the Rust ecosystem" where Lua is limiting; still an FFI plugin, subject to the host's
  abort-on-panic.

What maps directly onto a Lua-scripted multiplexer: `vim.pack`'s spec + lockfile + events
+ confirmation flow; `vim.lsp.enable`-style declarative enablement; `checkhealth`; the
`vim.schedule` rule; typed, generated API docs; builtins that make the tool complete
without plugins (palette, hints, floats, layouts, statusline) and a plugin layer for the
rest.

## 11. What to evaluate before writing an intent

1. A VT throughput benchmark through a pty (vtebench/termbench scenarios: ASCII, UTF-8
   heavy, SGR heavy, scrolling, resize) as the number every other question is measured
   against, including where the time splits between parser stage and apply/draw stage.
2. A bindgen run over the C23 headers: what `constexpr`, `nullptr`, fixed enums and
   `static inline` produce, and whether `--wrap-static-fns` covers the inline helpers.
3. The sanitizer link matrix: gcc + `-static-libasan` and clang, with an uninstrumented
   Rust staticlib, on Linux and macOS.
4. `regex` versus POSIX `regcomp` semantics on the patterns users actually write
   (alternation, `\1`, classes), to decide whether a linear-time engine is a compatible
   replacement or a documented change.
5. `unicode-width` (Unicode 17) versus the host `wcwidth`/utf8proc on the emoji, regional
   indicator and Hangul cases upstream tmux 3.6 special-cases.
6. Miri and Kani on a packed-cell codec and a UTF-8 decoder, as the template for how leaf
   modules are verified.
7. A `cdylib` loaded through LuaJIT `ffi` from a plugin, to settle whether native plugins
   need any core support at all.

## 12. Features the field has in 2026 that only become reachable with a Rust leaf (added 2026-09-09)

Candidates, not decisions. Each names the Rust piece that makes it possible and the evidence.

1. **Grapheme clusters, mode 2027, negotiated both ways.** Terminals moved to grapheme
   clustering (Ghostty, WezTerm, foot, Contour, Windows Terminal support DECSET 2027);
   tmux still counts `wcwidth` per codepoint, so a farmer emoji (three codepoints) is 4
   cells inside the multiplexer and 2 outside, and cursors drift. A multiplexer is the
   worst place for the mismatch and the only place that can fix it for every program: a
   grid that stores multi-codepoint graphemes as one cell (`unicode-segmentation`, Unicode
   17 tables in the tree), advertises 2027 to panes and requests it from the outer terminal,
   with `wcwidth` fallback when the outer terminal lacks it. This is the Rust `utf8/` +
   `grid/` modules with a design change tmux cannot make cell by cell. No multiplexer does
   it today.
2. **Scrollback that costs 6% of its size.** Ghostty's PageList: page-aligned mmap'd blocks
   from a pool, and since 2026 idle-time LZ4 compression of pages outside the viewport
   (250 ms debounce, ~26 µs to decompress a page, 93.9% saving on a 7.3 MB corpus, zero IO
   throughput regression, `MADV_DONTNEED`/`MADV_FREE_REUSABLE` to give physical memory
   back while keeping the mapping). termo's default is 50k lines per pane; with twenty
   panes that is the difference between hundreds of MB and tens. This is the Rust `grid/`
   history design (pages, not one `realloc`'d array of lines; `lz4_flex` or a vendored
   codec); it is not reachable by patching `grid.c`.
3. **Pane recording and live streaming, natively.** asciinema 3.0 is a Rust rewrite with
   asciicast v3 (JSON header, timestamped output/resize/marker events) and live streaming;
   its VT emulator is the `avt` crate. termo can `record-pane` to asciicast with OSC 133
   marks as markers and `stream-pane` to a viewer, and replay a recording inside a pane
   by feeding it to its own parser without a pty. `avt` doubles as a second oracle for
   differential fuzzing of the Rust parser. Greenfield Rust; no tmux twin.
4. **An agent-aware multiplexer.** The 2026 pattern is agents in panes driven over tmux
   (tmux-mcp, tmux-bridge-mcp, Herdr, wmux, Termdock). Herdr's complaint is the product
   gap: tmux "has no idea that the process in a pane is an AI agent, so it cannot tell you
   whether that agent is working or stuck"; it tracks idle/working/blocker per pane from
   screen manifests and lifecycle hooks, exposes a newline-delimited JSON socket (`spawn`,
   `run`, `read`, `wait agent-status`) and fires desktop notifications on blockers. Claude
   Code has an open request to emit OSC 133. termo has the primitives (control mode, OSC
   133 parsing, events, `run-lua -j`); what is missing is the typed API and the state
   machine: a `termo-mcp`/JSON-RPC server in Rust over control mode, pane states
   (`command-started`, `command-finished{exit, duration}`, `waiting-for-input`) derived
   from OSC 133 plus idle heuristics, notifications (OSC 9/777 to the outer terminal).
   Greenfield Rust out of process; the events are C/Lua.
5. **Sandboxed panes.** Codex uses Seatbelt on macOS and Landlock + seccomp on Linux;
   Claude Code ships its own sandbox runtime; Sandlock (2026) and ai-jail do it with
   unprivileged primitives; the Birdcage crate wraps Landlock and `sandbox-exec` in one
   API. A multiplexer that offers `new-pane -S <profile>` (filesystem allowlist, network
   off, env scrubbed) at spawn is a product feature none of the multiplexers has, and it
   is exactly what a user running three agents in three panes wants. Policy engine and
   profiles in Rust (`landlock`, `seccompiler`, Seatbelt profile generation), applied in
   the child between `fork` and `exec`; the C `spawn` path calls one function.
6. **Roaming reconnect over QUIC.** `etr` reimplements Eternal Terminal in Rust over QUIC:
   session keyed by ID and passkey, not address; the server keeps state and replays
   unacknowledged output on reconnect; one QUIC stream per forward so a slow one cannot
   stall the terminal. termo already keeps all state server-side; a `termo attach
   quic://host` client (`quinn`) gives mosh-style roaming plus tmux semantics in one
   binary. Greenfield Rust, the WezTerm "domain" idea with a transport that survives
   network changes.
7. **`nucleo` as the matcher.** Helix's fuzzy matcher: fzf's scoring with a two-matrix
   Smith-Waterman that finds the optimal match more often, ~6x faster than skim,
   Unicode-correct. Replaces `fuzzy.c` for the palette and makes fuzzy search across the
   whole scrollback and across sessions feasible in-process.
8. **Command blocks.** OSC 133 marks turn scrollback into blocks (Warp's model; Ghostty
   has a discussion on visual segmentation). tmux issue #5237 asked for OSC 133 to be
   forwarded to the outer terminal without `allow-passthrough` and is marked done
   upstream. With marks in the grid (Rust `grid/`) termo can offer a blocks view in copy
   mode (collapse output, copy last output, jump), per-command duration and exit status in
   the status line, and the events of item 4.

Order of leverage: 4 and 5 are product features with no competitor among multiplexers and
mostly greenfield code; 1 and 2 are why the grid should be designed in Rust rather than
ported; 3, 6, 7 are cheap once a Rust toolchain is in the tree; 8 falls out of 1 plus OSC
133.

## Sources

Rust: [1.98.0](https://blog.rust-lang.org/2026/08/20/Rust-1.98.0/),
[1.97.0](https://blog.rust-lang.org/2026/07/09/Rust-1.97.0/),
[1.85.0 and Rust 2024](https://blog.rust-lang.org/2025/02/20/Rust-1.85.0/),
[releases.rs 1.81–1.96](https://releases.rs/),
[Edition guide: unsafe extern](https://doc.rust-lang.org/edition-guide/rust-2024/unsafe-extern.html),
[unsafe attributes](https://doc.rust-lang.org/edition-guide/rust-2024/unsafe-attributes.html),
[static mut references](https://doc.rust-lang.org/edition-guide/rust-2024/static-mut-references.html),
[RPIT capture rules](https://blog.rust-lang.org/2024/09/05/impl-trait-capture-rules/),
[macro metavar expressions RFC 3086](https://rust-lang.github.io/rfcs/3086-macro-metavar-expr.html),
[Little Book of Rust Macros](https://lukaswirth.dev/tlborm/decl-macros/minutiae/metavar-expr.html),
[likely/unlikely tracking #151619](https://github.com/rust-lang/rust/issues/151619),
[sanitizer tracking #123615](https://github.com/rust-lang/rust/issues/123615),
[2026 sanitizer goal](https://rust-lang.github.io/rust-project-goals/2026/stabilization-of-sanitizer-support.html),
[ASan runtime mixing](https://geo-ant.github.io/blog/2024/rust-address-sanitizer-with-c/),
[mpmc tracking #126840](https://github.com/rust-lang/rust/issues/126840),
[portable SIMD blockers](https://github.com/rust-lang/portable-simd/issues/364),
[C-unwind RFC 2945](https://rust-lang.github.io/rfcs/2945-c-unwind-abi.html),
[Tree Borrows (PLDI 2025)](https://iris-project.org/pdfs/2025-pldi-treeborrows.pdf),
[Miri paper (POPL 2026)](https://research.ralfj.de/papers/2026-popl-miri.pdf),
[Kani paper (2026)](https://arxiv.org/abs/2607.01504),
[cargo-fuzz](https://rust-fuzz.github.io/book/cargo-fuzz.html),
[Rust Performance Book: build configuration](https://nnethercote.github.io/perf-book/build-configuration.html),
[cargo-pgo](https://kobzol.github.io/rust/cargo/2023/07/28/rust-cargo-pgo.html),
[arc-swap performance](https://docs.rs/arc-swap/latest/arc_swap/docs/performance/index.html),
[io_uring runtimes](https://users.rust-lang.org/t/status-of-tokio-uring/114481),
[regex crate](https://docs.rs/regex/latest/regex/),
[bindgen changelog](https://github.com/rust-lang/rust-bindgen/blob/main/CHANGELOG.md),
[kernel Rust layering](https://docs.kernel.org/rust/general-information.html),
[mlua](https://github.com/mlua-rs/mlua), [vte](https://docs.rs/crate/vte/latest).

Build: [Meson Rust module](https://mesonbuild.com/Rust-module.html),
[Meson Rust](https://mesonbuild.com/Rust.html),
[Meson 1.11 notes](https://mesonbuild.com/Release-notes-for-1-11-0.html).

Terminals: [alacritty event_loop.rs](https://docs.rs/alacritty_terminal/latest/src/alacritty_terminal/event_loop.rs.html),
[wezterm mux/src/lib.rs](https://github.com/wezterm/wezterm/blob/main/mux/src/lib.rs),
[wezterm localpane.rs](https://github.com/wezterm/wezterm/blob/main/mux/src/localpane.rs),
[wezterm plugins](https://wezterm.org/config/plugins.html),
[zellij plugins](https://zellij.dev/documentation/plugins),
[Ghostty devlog 006](https://mitchellh.com/writing/ghostty-devlog-006),
[libghostty](https://mitchellh.com/writing/libghostty-is-coming),
[Helix Steel PR #8675](https://github.com/helix-editor/helix/pull/8675),
[vtebench](https://github.com/alacritty/vtebench), [termbench](https://github.com/cmuratori/termbench),
[tmux 3.6](https://github.com/tmux/tmux/releases/tag/3.6),
[CVE-2020-27347](https://vulmon.com/vulnerabilitydetails?qid=CVE-2020-27347),
[CVE-2022-47016 rejected](https://cveawg.mitre.org/api/cve/CVE-2022-47016),
[Wasmtime April 2026 advisories](https://bytecodealliance.org/articles/wasmtime-security-advisories).

Section 12: [Ghostty scrollback compression PR #13264](https://github.com/ghostty-org/ghostty/pull/13264),
[Grapheme clusters in terminals](https://mitchellh.com/writing/grapheme-clusters-in-terminals),
[mode 2027](https://vtdn.dev/docs/decset/mode2027-grapheme/),
[asciinema 3.0](https://blog.asciinema.org/post/three-point-o/),
[Herdr, agent-aware sessions](https://dotzlaw.com/insights/claude-code-13-herdr-parallel-agent-sessions/),
[tmux-mcp-server](https://github.com/lox/tmux-mcp-server),
[Claude Code OSC 133 request](https://github.com/anthropics/claude-code/issues/32635),
[tmux OSC 133 forwarding #5237](https://github.com/tmux/tmux/issues/5237),
[Sandlock](https://arxiv.org/html/2605.26298v1),
[coding agent sandboxes list](https://gist.github.com/wincent/2752d8d97727577050c043e4ff9e386e),
[etr, Eternal Terminal over QUIC in Rust](https://github.com/l1a/etr),
[nucleo](https://github.com/helix-editor/nucleo).

Neovim: [0.12 overview](https://dotfiles.substack.com/p/whats-new-in-neovim-012),
[vim.pack guide](https://echasnovski.com/blog/2026-03-13-a-guide-to-vim-pack),
[nvim-oxi](https://github.com/noib3/nvim-oxi).
