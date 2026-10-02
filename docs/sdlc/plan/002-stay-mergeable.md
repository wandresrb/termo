# Plan 002: stay mergeable with tmux

- Status: Draft 2026-09-30
- Intent: [`intent/002-stay-mergeable.md`](../intent/002-stay-mergeable.md)
- Spec: [`specs/002-stay-mergeable.md`](../specs/002-stay-mergeable.md)

Steps land as commits on `termo-next`. Every step is additive: new files under `contract/`,
`tools/`, `tests/contract/` and `.github/`, none of them a file tmux owns. The reference
build is unmodified tmux at `5a820e63`, built with autotools in the sibling worktree
`../termo-tmux-base` (`sh autogen.sh && ./configure && make`; needs `automake` and
`autoconf`).

| Step | Contents | Proof |
|---|---|---|
| 1 | **Regress baseline.** `contract/regress-baseline.txt`: each script of upstream's `regress/` as pass or fail on the unmodified build, with the tmux sha and the host, from two runs of `make -j4` inside `regress/` (a script that differs between runs is marked flaky) | The file lists every script; the first run already taken on the unmodified build gave 171 pass and 1 fail (`prompt-words-history.sh`, the same in 3 of 3 runs); the second run gives the same |
| 2 | **The check.** `tools/check-tmux-files.sh` (spec §1) and `contract/allowed-edits.txt`, empty | `tests/contract/check-tmux-files.sh` passes its eight cases (spec Verification); on `termo-next` today it exits 0 |
| 3 | **CI.** Per push: the check. Release and after a merge of upstream: `regress/` (`make -j4`) against termo's binary compared with `regress-baseline.txt` | The per-push job is green on `termo-next` and never starts `regress/`; the release job turns red on a failure that is not in `regress-baseline.txt` (verified once with a deliberate break, reverted); an edit of a tmux file with no allowlist entry turns the per-push job red (verified once, reverted) |
| 4 | **Rules.** `ROADMAP.md`, `CLAUDE.md` and `SYNCING.md` state: the merge is the mechanism; tmux's files stay identical to upstream except the allowlist; termo's behaviour differs through the Lua and Rust layers and is not checked against tmux; a module diverges only by recorded decision, and then it gets a contract (spec §5); `regress/` is left untouched and runs on release and after a merge; "Rust" stays with the host plan. `SYNCING.md` gains the list of modules out of the merge zone, empty | The three files are consistent with the intent and the spec (read against them) |

## Not in this plan

The contract of a diverging module (spec §5: baseline dumper, black-box tests, verifier, rustdoc
comparator, coverage map) is written when the first module is replaced or modified, as its own
plan, with that module's own spec. Everything measured for it is in the spec so it is not
lost.

## Order

1 and 2 start together; 3 needs 1 and 2; 4 last.

## Files

`contract/allowed-edits.txt`, `contract/regress-baseline.txt`, `tools/check-tmux-files.sh`,
`tests/contract/check-tmux-files.sh`, `.github/workflows/` (a per-push job and a release
job), `ROADMAP.md`, `CLAUDE.md`, `SYNCING.md`. No change to any file present in
`upstream/master`.

## Risks

- **Flaky regress.** Upstream's scripts use `sleep` and depend on timing; under `-j4` some
  fail on a loaded host. Step 1 runs twice and marks differences as flaky, so the release
  job does not fail on them.
- **The check needs upstream's history.** The merge base needs the `upstream` remote fetched
  and a full clone. The per-push job fetches `upstream` and uses `fetch-depth: 0`.
- **An edit on the allowlist changes what tmux does.** The allowlist says which files
  differ, not whether the edit respects tmux's behaviour; each entry is reviewed when it is
  added, must be additive and guarded by `#ifdef TERMO`, and the release tier runs
  `regress/` against the result.
- **`git merge` conflicts.** They can only occur in an allowlisted file; each allowlisted
  edit is kept small so the conflict is small.
- **termo's suite does not yet cover what `regress/` covers.** `regress/` stays the release
  check; it is moved out of the per-push tier, not dropped.

## Close

The item closes when steps 1 to 4 are landed and the per-push job is green on `termo-next`.
