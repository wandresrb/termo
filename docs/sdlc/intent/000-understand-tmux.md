# Intent 000: understand tmux before deciding anything

- Status: Draft 2026-10-01
- Author: wandresrb
- Branch: `termo-next`
- Comes before: 001 (project foundation: how we work) and 002 (stay mergeable with tmux)

## Problem

termo is about to decide what to keep from tmux, what to replace and what to add, and nobody
has written down how tmux works, how it is developed, where it hurts, or what it leaves
unsolved. The first attempt (`refactor/to-termo`) took large decisions early (a new source
layout, Meson, C23 across tmux's files, Rust as a library under C) without that
understanding, and had to abandon the layout and take Rust out of the tree. Understanding is
a phase of its own, because it is more than reading the code: it includes how the project
is run, why it is built the way it is, and what that costs.

## Outcome

A set of documents, each backed by evidence, that answers the questions below, and a closing
list of the decisions the next intents need. When that list is written the phase ends. It
does not run until everything is known: a question that cannot be answered with evidence is
recorded as open and left.

## Questions

1. **Purpose.** What problem tmux solves, for whom, and what it deliberately does not do.
2. **How the project is developed.** Upstream is OpenBSD's source tree; `tmux-openbsd-cutover`
   holds its history filtered to `usr.bin/tmux/`; the portable repository merges that and
   adds the portability layer, autotools, `regress/` and documentation (`SYNCING.md`). Who
   decides, how a change is proposed and accepted, review culture, release cadence, how
   issues are handled (`.github/CONTRIBUTING.md`), commit size and rhythm from `git log`.
3. **Architecture.** Process model (client and server), the event loop, the object graph,
   the command queue, terminal input and output, the grid and screen, key tables, options,
   formats, modes, control mode, jobs. Where each is, drawn as diagrams and tied to files.
4. **Design decisions and their consequences.** For each decision (one event loop, the
   server owns all state, text configuration, commands queued asynchronously...), what it
   buys and what it costs.
5. **Bottlenecks.** Where time and memory go: output-heavy panes, many clients, scrollback,
   resize. Measured on the unmodified build, not guessed. `refactor/to-termo` has a
   benchmark (`tools/bench/`) that can be read and adapted.
6. **What users and tools rely on.** The commands, formats, options, control mode, `TMUX`
   variables and config search order that scripts and plugins (TPM, tmux-resurrect,
   vim-tmux-navigator...) depend on. This is the compatibility surface termo must not
   break by accident.
7. **The gap as a product.** What people ask tmux for and do not get, with evidence from
   its issue tracker, and what comparable tools offer (Zellij, WezTerm, Ghostty).
8. **Platforms.** Which platforms tmux supports, how big the portability layer is, and what
   it costs to carry.

## Method

For each question: read the source and the history, draw it, write the document, and attach
the evidence. A claim carries a file and line, a number measured on the build, or a link;
anything else is marked unverified. Where a document states a fact about behaviour, one
check or measurement on the build backs it.

## Deliverables

New files under `docs/tmux/`, one per question (`purpose`, `development`, `architecture`,
`decisions`, `bottlenecks`, `compatibility`, `gap`, `platforms`) and a last one,
`decisions-for-termo.md`, with the list that closes the phase: what stays tmux, what is
replaced, what is added, which bottlenecks are real, what contract users rely on, and which
parts of tmux's way of working termo adopts.

## Scope

Reading, measuring and writing. No change to any file tmux owns and no termo code. The build
used for measurements is unmodified tmux at `5a820e63` (`../termo-tmux-base`).

## Sources already at hand

`refactor/to-termo` is kept as a reference of what was tried. Its `docs/sdlc/research/006-rust.md`
and the product table in `docs/sdlc/intent/006-rust.md`, the architecture section of its
`CLAUDE.md`, and `tools/bench/` are starting points. They are read through `git show
refactor/to-termo:<path>` and each claim taken from them is checked against the code before
it is reused.

## Constraints

- Every document is a new file; nothing upstream owns is modified.
- Facts are cited; opinions are marked as opinions.
- The closing list says which question each decision comes from.

## Evals

- Each document lists its open questions.
- For each document, five claims chosen at random are checked against the source or the
  build; any that fails is fixed before the phase closes.
- `decisions-for-termo.md` answers every decision the intents 001 and 002 ask of it.

## Open

- How to get evidence from the issue tracker (the `gh` CLI, or reading the pages).
- How deep to go on rendering and the terminal layer, where most of the cost is likely to be.
