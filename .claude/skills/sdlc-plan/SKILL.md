---
name: sdlc-plan
description: Start work on an SDLC item NNN: read its intent and spec, write docs/sdlc/plan/NNN-*.md and stop for sign-off before any code.
---

Given an item number $ARGUMENTS:

1. Read `docs/sdlc/intent/NNN-*.md` and `docs/sdlc/specs/NNN-*.md`. If either is
   missing, say so and stop; do not invent one.
2. Read the code the spec names before planning. Prefer what exists (search for
   the function first) over new mechanisms.
3. Write `docs/sdlc/plan/NNN-<slug>.md` with: `Status: Draft`, the exact files
   to create or change, the order of PRs, risks, and for each step the eval that
   proves it (which regress scripts, unit test, integration test, snapshot or
   fuzz target).
4. Stop and ask for sign-off. No implementation code until the plan's status is
   `Approved`.
