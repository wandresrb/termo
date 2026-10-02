# Intent 002: stay mergeable with tmux

- Status: Draft 2026-09-30
- Author: wandresrb
- Branch: `termo-next`, cut from `upstream/master` at `5a820e63`

## Problem

termo's first attempt (`refactor/to-termo`, kept as the reference of what was tried) moved
`src/` into domains, swept tmux's files to C23 and replaced autotools with Meson before
anything could prove tmux's behaviour still held. The diff against tmux reached 690 files
(+26,588 / −31,355), upstream changes stopped merging, every fix became a hand port, and
on 2026-09-30 the branch had failures that nobody could attribute to termo or to tmux: five
scripts of upstream's regress failed on it and all five pass on unmodified tmux, so they
were termo's.

Upstream is busy: 696 commits in the three months to 2026-09-30, 455 of them in C, about a
hundred fixes. A fork that cannot merge that stream falls behind in weeks.

## Outcome

termo is a fork with **controlled divergence**:

- tmux's files stay as upstream has them, with tmux's layout and autotools build, and
  `git merge upstream/master` is how upstream enters. The gain of keeping compatibility is
  exactly that: everything not decided to diverge merges in.
- termo's own code lives in new files: C23 in termo's own modules only, and a `meson.build`
  added next to autotools, so each person builds with either.
- termo's behaviour differs from tmux's on purpose, through the Lua and Rust layers added
  on top. That is the product: changing behaviour without touching the core. It is not
  checked against tmux and is not a divergence of the core.
- A module leaves the merge zone only when it is **replaced or modified** instead of added
  to, and that decision is taken module by module and recorded in `SYNCING.md`.

## The check

For everything that does not diverge, the contract is tmux's code itself, and the check is a
`git diff`: every file that exists in upstream's tree is identical to upstream's, except the
files on a short allowlist, each with its reason. The diff is taken against the upstream
commit last merged (the merge base with `upstream/master`), so it reports only what termo
changed and is incremental by nature: nothing is recomputed, git lists what differs.

- A file of tmux's that differs and is not on the allowlist fails the check.
- Files termo adds are free: they are not tmux's.
- Edits termo needs in a tmux file to hook itself in (see below) are the allowlist, kept
  small, each guarded so the autotools build of the same tree still equals tmux.

The check runs on every push, takes milliseconds, and never runs what did not change.

## When a module diverges

Only then does a machine-readable contract of that module become necessary: what it promised
before it was replaced or modified. It is written at that moment, for that module, and runs
only when that module's files change (`git diff --name-only`). The design is kept in the
spec (baseline dumper linked against tmux's objects, black-box tests against the binary, a verifier
that separates additive from breaking changes, rustdoc's JSON compared with the baseline,
`cargo-semver-checks` for the module's crate, `abidiff` for its boundary with the C core)
and is not built until a first module diverges.

## Tests

tmux's `regress/` stays exactly as upstream has it, so merges bring its changes for free. It
is slow (172 scripts, 990 s of running time, 754 `sleep` calls, serial in upstream's
Makefile), so it runs on release and after a merge of upstream, not on every push. termo's
own suite is separate, built with Meson, parallel and without `sleep`, and is the per-push
gate. Neither edits the other's files. termo's suite does not replace `regress/` until it
covers the same behaviour.

## Scope

The check script, the allowlist, a baseline of upstream's regress on unmodified tmux, the CI
jobs, and the rules in `ROADMAP.md`, `CLAUDE.md` and `SYNCING.md`. No change to tmux's files.

## Constraints

- No file upstream owns is modified by this item.
- Nothing in the check runs tmux or termo: it is `git`, a list and a comparison.
- The allowlist grows only with a reason next to each entry.

## Evals

- A fixture repository with an upstream branch: editing a tmux file fails the check, the
  same edit on the allowlist passes, adding a new file passes, and the merge base moves
  after a merge so merged upstream changes do not count as termo's.
- Upstream's regress on unmodified tmux at `5a820e63`: the result is recorded as the
  baseline (171 pass, 1 fails: `prompt-words-history.sh`, the same in 3 of 3 runs).

## Decisions (2026-09-30)

- The contract of a diverging module is TOML when it is written; the format of the allowlist
  is plain text, one entry per line.
- The merge is the mechanism; the contract comes with the tmux code that is merged.

## How the Lua runtime hooks into tmux (read at `5a820e63`)

Most of what the Lua runtime needs is reachable from new files through functions tmux
already exports in `tmux.h`, so it needs no edit:

- **Events**: upstream's own bus. `events_add_sink(name, cb, data)` (`events.c`, added
  upstream in 2026) is how `hooks.c`, `control-notify.c` and `wait-for` subscribe; a Lua
  sink is one more subscriber. 38 events fire today (`client-attached`,
  `pane-focus-in`, `window-layout-changed`, `session-closed`, ...).
- **UI**: `menu_display`, `popup_display`, `status_prompt_set`, `status_message_set`.
- **Formats and options**: `format_single`, `options_get`, `options_set_*`, and the global
  lists of sessions, windows and panes.
- **Timers and processes**: libevent timers and `job_run`.
- **Lua start and stop**: the Lua state is created on first use, so `server.c` is not
  touched; the process exit frees it.
- **Config**: `TMUX_CONF` and `TMUX_SOCK` are build defines (`Makefile.am`, `tmux.h`), so the
  Meson build sets termo's search order and socket directory without a source change. A
  `.lua` file cannot sit in that list (it is parsed as tmux commands), so termo's default
  config calls `run-lua -f` on `init.lua` when it exists.

Three things have no entry point and need a small edit in a tmux file. They are the first
candidates for the allowlist, each additive to tmux's behaviour:

1. **The `run-lua` command.** `cmd_table` in `cmd.c` is a fixed array with no registration:
   one entry there and one `extern` in `tmux.h`, inside `#ifdef TERMO` so the autotools
   build of the same tree is tmux. Unavoidable, since every binding to a Lua function goes
   through this command.
2. **Command output for `termo.cmd()`.** `cmdq_print_data` hands output to
   `server_client_print(item->client, ...)`, which has nowhere to put it without a client.
   A capture hook is about five lines in `cmd-queue.c`. Alternative with no edit:
   `termo.cmd()` returns nothing, and Lua reads state through formats and the list
   functions.
3. **Formats defined in Lua** (`#{hints}`, `#{palette}`). `format_table` is static and
   `format_create` has no hook: about two lines in `format.c`. Alternative with no edit:
   Lua writes the status line as tmux conditionals on `#{client_key_table}`, which tmux
   evaluates per client itself, and keeps `@` user options up to date for the rest.

Item 1 is taken. Items 2 and 3 are decided in the Lua runtime's own intent.

## Open

- Whether the check also lists files that are on the allowlist but no longer differ, so the
  list does not go stale.
