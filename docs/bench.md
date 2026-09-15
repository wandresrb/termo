# VT throughput benchmark

`tools/bench/bench.py` feeds fixed payloads through a pane of a real server and measures
how fast termo consumes them, with and without a client drawing the result. It exists so
that the roadmap rule "hot paths never regress in throughput" is a number: every change to
the parser, the grid, screen-write or the tty path is compared against the run before it,
and the plan for item 006 gates each Rust module on it.

```sh
just bench                 # release build in build-bench/, every scenario, 3 runs each
just bench ascii utf8      # a subset
just bench-compare <sha>   # build/bench/<sha>.json against the current HEAD's file
```

`just bench` writes `build/bench/<git sha>.json`. `bench-compare` prints one line per
scenario and measurement with the ratio and exits 1 when any measurement is more than 10%
worse. Run it on the same machine, idle, with the same build type on both sides; the
numbers are not portable across machines and a sanitized build is about 50 times slower
than release. `--runs`, `--scale` (payload size multiplier) and `--termo` are the knobs;
`--history` is the `history-limit` of the bench server (50000, termo's default).

## What is measured

Each scenario is a directory under `tools/bench/scenarios/` with an executable `benchmark`
that writes the payload to stdout (vtebench's shape; `setup` is optional). Payloads are
deterministic (seeded) and cached in `build/bench/payloads/`. The bench server runs
`-f` with only `history-limit` and `allow-set-title`, and one pane whose shell reads a
file name and a nonce, `cat`s the file and sets the pane title to `done-<nonce>`.

Three measurements per scenario:

- `detached`: no client attached. The clock starts when the file name is sent to the pane
  and stops when `#{pane_title}` reports the nonce, polled every 50 ms. This is the parse
  and grid stage alone.
- `attached-80x24` and `attached-200x60`: a client on a pseudo-terminal of that size,
  drained continuously. The clock stops when the `BENCH-END-<nonce>` line the shell prints
  after the payload has been drawn to the pty. The difference to `detached` is the redraw
  and tty cost.

Reported per measurement: bytes, the seconds of each run, `median_bps`, `best_bps`, and
the server's peak RSS in kB (`VmHWM` on Linux, sampled `ps` elsewhere).

| Scenario | Payload | Exercises |
|---|---|---|
| `ascii` | 32 MiB of 79-column printable lines | the print fast path, line wrap, history scroll |
| `utf8` | 16 MiB of Latin, CJK, Hangul, Greek, Cyrillic, combining marks, emoji ZWJ, flags and skin tones | UTF-8 decode, widths, combining, extended cells |
| `sgr` | 16 MiB with an SGR change (256, RGB, attributes, reset) before every character | attribute parsing, extended cells, tty attribute changes |
| `scroll` | 32 MiB of short numbered lines | scrolling and history growth per line |
| `cursor` | 16 MiB of `CSI row;col H` plus one character | CSI dispatch, cursor moves, in-place cell writes |
| `sync` | 16 MiB of DEC 2026 frames redrawing 24 rows | synchronized output batching |
| `resize` | the `utf8` payload, then `resize-window -x` through ten widths (120, 60, 200, 40, 150, 80, 100, 30, 180, 80) | `grid_reflow` over the full history; reported in seconds, with the history size |

## Baselines

Recorded when a measurement changed what the tree does; the JSON files stay out of git.
Apple M-series, macOS 26, release build without sanitizers, 3 runs, medians.

| sha | ascii | utf8 | sgr | scroll | cursor | sync | resize (46k lines) |
|---|---|---|---|---|---|---|---|
| `b46a8ccd` detached | 51.2 MB/s | 11.9 MB/s | 25.9 MB/s | 40.2 MB/s | 28.2 MB/s | 46.9 MB/s | 0.42 s |
| `b46a8ccd` attached 80x24 | 41.2 | 10.3 | 20.4 | 32.5 | 26.1 | 40.6 | 1.59 s |
| `b46a8ccd` attached 200x60 | 40.7 | 7.3 | 19.8 | 32.5 | 24.3 | 39.6 | 1.56 s |

Peak RSS at `b46a8ccd`: 29 MB after 32 MiB of `ascii`, 61 MB after 16 MiB of `utf8`,
364 MB with the `utf8` payload held in a 46k-line history for `resize`. What the first
baseline says: `utf8` runs at a quarter of `ascii` (every non-ASCII cell leaves the print
fast path and becomes an extended cell), the client redraw costs 20% on `ascii` and up to
40% on `utf8` at 200x60, and reflowing 46k lines of mixed-width text ten times takes 0.4 s
detached and 1.6 s with a client.

## What the bench decided in plan 006 step 2

- Pane read size (`bufferevent_set_max_single_read`, default 4096): swept at 4K, 16K,
  64K and 256K on `ascii`, `scroll` and `utf8` detached, 5 runs each. 4K and 256K are
  equal (51.1 / 40.3 / 11.8 MB/s); 16K and 64K are 8 to 12% slower on `ascii`. The read
  size is not where the time goes, so the tree keeps libevent's default and the
  per-iteration parse budget the spec proposed is not needed: one read per pane per loop
  iteration already bounds it.
- `linedata` geometric growth (`grid.lalloc`): `scroll` 40.3 → 46.2 MB/s detached
  (+15%), `ascii` and `utf8` unchanged, `resize` and RSS within run-to-run noise
  (0.43 to 0.46 s, 343 to 368 MB either way). Landed.
- The `since_ground` cap and the control-mode line cap are security fixes with no
  throughput effect; not measured.

## Limits

vtebench's own caveat applies: this measures the speed at which termo reads from the pty,
not latency or frame rate. The `detached` clock includes one command round trip per poll
(about 10 ms), so a scenario should run for seconds, which the default sizes do on a
release build. `resize` times ten synchronous `resize-window` commands; with a client
attached it also waits for the pty to go quiet after each, so the attached numbers include
the redraw of every width.
