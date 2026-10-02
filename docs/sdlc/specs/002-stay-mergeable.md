# Spec 002: stay mergeable with tmux

- Status: Draft 2026-09-30
- Intent: [`intent/002-stay-mergeable.md`](../intent/002-stay-mergeable.md)
- Plan: [`plan/002-stay-mergeable.md`](../plan/002-stay-mergeable.md)

## Facts the design rests on

Measured on 2026-09-30 against an unmodified tmux build of `5a820e63` (`tmux -V` reports
`next-3.9`), built with autotools in the sibling worktree `../termo-tmux-base`.

- Upstream merges often: 696 commits in three months, 455 in C, about a hundred fixes.
- Upstream's `regress/` on the unmodified build, `make -j4`: 172 scripts, 171 pass, 1 fails
  (`prompt-words-history.sh`, the same failure in 3 of 3 runs, so a tmux failure at this
  commit and not a timing one). The scripts add up to 990 s of running time, 59 of them take
  20 s or more, and they contain 754 `sleep` calls: they synchronise by waiting a fixed
  time. Upstream's Makefile runs them one after another; the sockets carry the PID, so
  running them in parallel works. The five scripts that failed on `refactor/to-termo` all
  pass on unmodified tmux.
- `git merge-base HEAD upstream/master` is the upstream commit last merged, as long as
  upstream only enters by merge. On `termo-next` it is `5a820e63` today.
- tmux owns 1 file per concept in the repository root (`cmd-*.c`, `window-*.c`, `tty-*.c`,
  `tmux.h`, `Makefile.am`, `regress/`, ...), so "a file tmux owns" is exactly "a path in
  upstream's tree".

## 1. The check

`tools/check-tmux-files.sh [--base <ref>]`:

1. `base` defaults to `git merge-base HEAD upstream/master`.
2. `owned` is the set of paths in `git ls-tree -r --name-only <base>`.
3. `changed` is `git diff --name-only <base> -- <owned>`, which includes files termo edited,
   deleted or renamed away, and is empty when termo has touched nothing.
4. `allowed` is the paths in `contract/allowed-edits.txt`.
5. The check fails, printing each path in `changed` not in `allowed` with `git diff --stat`,
   and exits 0 otherwise.

Files termo adds are not in `owned`, so they are free. Changes that merges bring in are not
termo's, because the base moves to the merged commit with each merge. The working tree,
staged and committed changes are all included (`git diff <base>`, not `<base>..HEAD`), so the
check catches an edit before it is committed.

Cost: one `git ls-tree` and one `git diff`; no build, no server. It runs on every push.

### Allowlist

`contract/allowed-edits.txt`, one entry per line: `path`, a tab, the reason, and the
section of the intent or `SYNCING.md` that records it. Comments start with `#`. The file is
empty at first. Each edit it names is:

- additive to tmux's behaviour (a new command, event or capability; nothing tmux did
  changes);
- guarded by `#ifdef TERMO` when it adds a symbol, so the autotools build of the same tree
  compiles to tmux;
- as short as the hook requires.

The known candidates are in the intent (the `run-lua` entry in `cmd.c` and `tmux.h`; a
capture hook in `cmd-queue.c`; a hook in `format.c`); the last two are decided in the Lua
runtime's own intent.

Optional warning: an allowlisted path that no longer differs from `<base>` is reported so the
list does not hold stale entries. It does not fail the check.

## 2. Merging upstream

```
git fetch upstream
git merge upstream/master
tools/check-tmux-files.sh
```

A conflict can only happen in an allowlisted file, because every other file of tmux's is
identical to the previous upstream commit and merges as a fast change. After the merge the
base moves to the merged commit, so `changed` is again only what termo added to those files.
The release tier then runs `regress/` (section 4).

## 3. termo's behaviour

termo differs from tmux on purpose through code in new files: the Lua runtime, the Rust
host and whatever is added on top. None of this is checked against tmux. The only thing the
check states is that tmux's own code, the core, is the code upstream has.

## 4. Tests

| When | What | Notes |
|---|---|---|
| Every push | the check (section 1) and termo's own suite | termo's suite is built with Meson, parallel, without `sleep`, and outside this item |
| Release, and after every merge of upstream | `regress/` against termo's binary, compared with `contract/regress-baseline.txt` | `make -j4` inside `regress/`; a failure not in the baseline fails the job |

`regress/` is tmux's and is not edited; termo's suite lives in its own directories. Neither
touches the other's files. termo's suite does not replace `regress/` until it covers the same
behaviour; how that is measured is decided when termo's suite is specified.

`contract/regress-baseline.txt` records, for each script, pass or fail on the unmodified
build, with the tmux commit and the host; a script that differs between two runs is marked
flaky. Its first content is 171 pass and 1 fail (`prompt-words-history.sh`).

## 5. Reserved: the contract of a module that diverges

Not built by this item. It is the design to use the first time a module is replaced or
modified in place, and it is written out here so the measurements are not lost.

- **Trigger.** The module's files change (`git diff --name-only`), and only then.
- **Baseline.** A dumper, a C file linked with the tmux objects, reads `cmd_table` (92
  commands) and `options_table` (269 entries), both exported; it does not edit any tmux
  file: `tmux.o`, the object that defines `main`, is copied with `objcopy
  --redefine-sym main=tmux_main` and the dumper supplies its own. This was tried and works
  on `5a820e63`. `struct cmd_entry` carries `name`, `alias`, `usage`, `args.template` (`c:dEf:rt:x`:
  a letter followed by `:` takes a value), `args.lower` and `args.upper`;
  `struct options_table_entry` carries `name`, `type`, `scope`, `minimum`, `maximum`,
  `choices`, `default_str`, `default_num`, `default_arr`. The tree it is given is upstream's,
  never the merged one, which carries termo's files. `format_table` is `static`, so format
  names are read from `format.c`, with a count of the `FORMAT_TABLE_` entries to fail loudly.
  Bindings come from `list-keys` on a `-f /dev/null` server (308 today), hooks from
  `show-hooks -g`. Output is TOML, one file, written by a deterministic writer.
- **Black-box tests against the binary.** They do not look inside, so they give the same answer
  whatever implements the module. tmux's argument errors are distinct and happen before a
  command runs: a value flag with no value (`split-window -c`) gives `-c expects an
  argument`, fewer arguments than the lower bound gives `too few arguments (need at least
  N)`, more than the upper bound gives `too many arguments (need at most N)`. Options give
  `unknown value`, `value is too small`, `value is invalid`. A variable of a format exists
  only if `display-message -a` lists it (an unknown `#{name}` expands to empty text).
  A test is only used when it cannot run the command.
- **Verifier.** It compares what those tests return with the baseline: something removed, a
  type or default changed, a value flag that stops taking a value, a raised minimum or
  lowered maximum, or a binding tmux lacks in `root`, `prefix`, `copy-mode` or
  `copy-mode-vi` is breaking; something new and declared is additive; something new and
  undeclared is breaking; an element with no such test is reported and never fails.
- **Rust module.** rustdoc's JSON (`--output-format json`, nightly) describes its public API
  and is compared with the baseline for the part of the contract the module implements,
  following rustdoc's format versions as `cargo-semver-checks` does; `cargo-semver-checks`
  covers the crate's API between versions; `abidiff` covers its boundary with the C core.
- **Coverage.** Which commands, options and formats `regress/` and termo's suite exercise,
  counted by name in their files, to rank what to test next and to know when termo's suite
  covers what `regress/` does.

The behaviour of a command is held by tests; the machine-readable shape only says what to
keep.

## Verification

- `tests/contract/check-tmux-files.sh`, run in a scratch repository built by the test: an
  `upstream` branch with a few tmux-like files and a `main` from it.
  1. No change: exit 0.
  2. Edit an owned file: exit 1 and the path is printed.
  3. The same edit with the path on the allowlist: exit 0.
  4. Add a new file: exit 0.
  5. Delete an owned file: exit 1.
  6. Advance `upstream` by one commit and merge it: exit 0, and the base is the new commit.
  7. An uncommitted edit of an owned file: exit 1.
  8. An allowlisted path that no longer differs: exit 0 with a warning.
- `regress/` on the unmodified build, twice: `contract/regress-baseline.txt` records the
  same failures both times and marks any that differ as flaky.
