# termo roadmap

Living plan for the C23/Rust modernization of tmux. Update this as phases complete or the plan changes — this is the source of truth for the split, not a one-time design doc.

## Method

Modeled on how Luca Palmieri (Mainmatter) approaches C/C++-to-Rust migrations (e.g. rewriting Redis's query engine):

1. **Start from the leaves.** Port modules with few or no dependencies on the rest of the system first, to work out the FFI/build/test mechanics before touching anything that matters.
2. **Treat the `unsafe`/FFI boundary as a design surface**, not glue code nobody reviews. Explicit ownership rule: whoever allocates, frees. Never allocate in C and free in Rust or vice versa.
3. **Validate through integration, not just unit tests.** Every ported module must keep `regress/` fully green before moving to the next one.
4. **Incremental, never big-bang.** The C tree keeps working at every commit. No module is "in flight" for long.

## Why not rewrite everything

- A full C23 rewrite alone buys little: tmux's C is already clean, warnings-as-errors and ASAN-tested in CI. C23 features (`nullptr`, `constexpr`, `[[attributes]]`) are syntactic sugar, not an architecture change.
- A full Rust rewrite duplicates [Zellij](https://github.com/zellij-org/zellij), which already is "tmux rethought in Rust" with its own design. Not the goal here.
- tmux's ~130 `cmd-*.c` files and the `session`/`window`/`window_pane`/`client` object graph are built on intrusive linked lists/RB-trees (`TAILQ`, `RB_HEAD`) with shared mutable ownership across the whole tree. Rust doesn't have a clean idiomatic mapping for that without a full ownership redesign (arenas + indices instead of pointers). Not attempting this — see "Stays in C" below.

## Stays in C (bumped to C23 where the compiler allows)

- `compat/` — portability shims for AIX, HP-UX, Solaris, Haiku, Cygwin. Rust toolchain support on these targets is poor-to-nonexistent; no upside, only portability risk.
- `server.c`, `server-client.c`, `proc.c`, `job.c`, `spawn.c` — the libevent event loop, fork/exec/pty/signal handling. Fragile across an FFI boundary (allocator mismatches around `fork()`), and this isn't where tmux's memory bugs live.
- `cmd-*.c` (all ~130 command implementations) and the core object graph (`session.c`, `window.c`, the `client`/`session`/`window`/`window_pane` structs). Deferred indefinitely — see above.
- `cmd-parse.y` — mature yacc grammar, not a safety hotspot, low priority.

## Rust candidates, in planned order

| Phase | Module | Why | Status |
|---|---|---|---|
| 0 | `regsub.c` | Pure leaf (126 lines, zero references to session/client/window). Proof of concept for the hybrid C+Rust build/link/test pipeline — nothing important is at risk here. | Not started |
| 1 | `utf8.c`, `utf8-combined.c` | Self-contained (no session/client coupling), but a real "hot module" — decodes every character rendered to screen. First module where the safety payoff actually matters. | Not started |
| 2 | `grid.c`, `grid-view.c`, `grid-reader.c` | The scrollback buffer engine. Raw `grid_cell` array manipulation, resizing, capacity growth — textbook UAF/off-by-one territory. Exposed as an opaque type behind the existing `grid_*()` C ABI so callers elsewhere don't change. | Not started |
| 3 | `input.c` | The VT100/xterm escape-sequence parser. Parses byte streams from the child pty — the single highest-value, highest-risk target (this class of parser is where real terminal-emulator CVEs happen historically). Only attempted once the pipeline is proven on 0–2. | Not started |
| — | `colour.c`, `style.c` | Small, pure parsers. Good intermediate exercises, can slot in wherever convenient. | Not started |
| — | `format.c` expression engine | The `#{...}` language's parser/evaluator (not the whole file) is a natural Rust fit, but must be split from the C-side value lookups (session/window/client state), which stay behind a callback/trait boundary. Bigger effort, later. | Not started |

## Phase 0 plan (next up)

Port `regsub.c` to Rust:

1. New `rust/` workspace crate, compiled as a `staticlib`.
2. Same C-callable signature as today's `regsub()` in `regsub.c`.
3. Wire into the existing autotools build (`Makefile.am`) so `make` produces one binary linking the Rust static lib — no Meson migration needed for this step.
4. `regress/` must stay green.
5. Write down the ownership rule for this specific boundary (who allocates the returned `char *`, who frees it) before merging.
