# termo

`termo` is an experimental fork of [tmux](https://github.com/tmux/tmux), the terminal multiplexer. It exists to explore incrementally modernizing tmux's C codebase — partly to C23, partly to Rust for the modules where memory safety actually matters — without doing a ground-up rewrite.

This is a learning/research project, not a drop-in replacement for tmux and not an attempt to get changes merged upstream. See [`ROADMAP.md`](ROADMAP.md) for the plan and the reasoning behind it.

## Why a fork, not a rewrite

tmux's correctness lives in ~20 years of accumulated fixes to terminal-emulation edge cases (see `CHANGES` in this tree) across xterm/vt100 quirks, exotic terminfo entries, and a dozen OSes. A from-scratch rewrite — in C23 or in Rust — would silently reintroduce bugs that are already fixed, with no test suite thorough enough to catch them all. So `termo` follows an incremental, module-by-module strangler-fig approach instead: the existing C tree keeps running, and individual self-contained modules get ported one at a time behind their existing C ABI, validated against tmux's own regression suite (`regress/`) at every step.

## Relationship to upstream tmux

This repo was cloned directly from tmux, so it carries the full upstream history and every file's original copyright/license header (tmux is ISC-licensed; a few files under `compat/` are BSD-3-clause — see `COPYING` and individual file headers). Two remotes are configured:

- `origin` — this fork (`wandresrb/termo`)
- `upstream` — the real [tmux/tmux](https://github.com/tmux/tmux) — used to pull in upstream fixes as they land, since the C parts of this tree are still, for the most part, tmux

## Build

Unchanged from tmux for now — see the original [`README`](README) file. No build-system or binary-naming changes have been made yet; this stays true until the roadmap's Phase 0 proof of concept is validated (see `ROADMAP.md`).

## Status

Early / planning stage. No Rust code has landed yet.
