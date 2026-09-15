#!/usr/bin/env python3
"""VT throughput benchmark: payloads through a pane, with and without a client.

  tools/bench/bench.py --termo build-bench/termo [scenario ...]
  tools/bench/bench.py --compare <sha> [--against <sha>]

Every run writes build/bench/<sha>.json; --compare exits 1 when any measurement
regressed by more than 10%.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "e2e"))

from termo import Pty, Server, expect  # noqa: E402

SCENARIOS = {
    "ascii": {"bytes": 32 << 20},
    "utf8": {"bytes": 16 << 20},
    "sgr": {"bytes": 16 << 20},
    "scroll": {"bytes": 32 << 20},
    "cursor": {"bytes": 16 << 20},
    "sync": {"bytes": 16 << 20},
    "resize": {"payload": "utf8", "widths": [120, 60, 200, 40, 150, 80, 100, 30, 180, 80]},
}
SIZES = [(80, 24), (200, 60)]
THRESHOLD = 0.10


def git_sha() -> str:
    r = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT,
                       capture_output=True, text=True)
    return r.stdout.strip() or "unknown"


def payload(name: str, size: int, scale: float, cache: Path) -> Path:
    want = int(size * scale)
    out = cache / f"{name}-{want}.bin"
    if out.exists() and out.stat().st_size >= want:
        return out
    cache.mkdir(parents=True, exist_ok=True)
    script = ROOT / "tools" / "bench" / "scenarios" / name / "benchmark"
    with open(out, "wb") as f:
        subprocess.run([sys.executable, str(script), "--bytes", str(want)], stdout=f, check=True)
    return out


class Rss:
    def __init__(self, pid: int):
        self.pid = pid
        self.peak = 0
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def sample(self) -> int:
        status = Path(f"/proc/{self.pid}/status")
        if status.exists():
            for line in status.read_text().splitlines():
                if line.startswith("VmHWM:"):
                    return int(line.split()[1])
            return 0
        r = subprocess.run(["ps", "-o", "rss=", "-p", str(self.pid)], capture_output=True, text=True)
        return int(r.stdout.strip() or 0)

    def run(self) -> None:
        while not self.stop.is_set():
            self.peak = max(self.peak, self.sample())
            self.stop.wait(0.1)

    def finish(self) -> int:
        self.stop.set()
        self.thread.join()
        self.peak = max(self.peak, self.sample())
        return self.peak


class Bench:
    def __init__(self, binary: Path, tmpdir: Path, history: int):
        self.binary = binary
        self.tmpdir = tmpdir
        self.conf = tmpdir / "bench.conf"
        self.conf.write_text(f"set -g history-limit {history}\nset -g allow-set-title on\n")

    def server(self, size: tuple[int, int]) -> Server:
        s = Server(self.binary, f"bench-{os.getpid()}-{uuid.uuid4().hex[:6]}", tmpdir=self.tmpdir)
        s.start(conf=self.conf, size=size,
                command="sh -c 'while read x n; do cat \"$x\"; printf \"\\033]2;done-$n\\a\\nBENCH-END-$n\\n\"; done'")
        expect(lambda: s.fmt("#{pane_pid}#{pane_dead}").endswith("0") and len(s.fmt("#{pane_pid}")) > 1,
               what="the bench shell")
        return s

    @staticmethod
    def quiet(client: Pty, idle: float) -> None:
        while client.read_some(idle):
            pass

    def feed(self, s: Server, file: Path, client: Pty | None) -> float:
        nonce = uuid.uuid4().hex[:8]
        start = time.perf_counter()
        s.cmd("send-keys", "-t", "main", "-l", f"{file} {nonce}")
        s.cmd("send-keys", "-t", "main", "Enter")
        if client is not None:
            client.wait_for(f"BENCH-END-{nonce}", timeout=600)
        else:
            expect(lambda: s.fmt("#{pane_title}") == f"done-{nonce}", timeout=600,
                   what="the payload to be parsed")
        return time.perf_counter() - start

    def payload_run(self, name: str, file: Path, runs: int, size: tuple[int, int],
                    attached: bool) -> dict:
        s = self.server(size)
        client = s.attach_pty(size=size, marker=None) if attached else None
        if client is not None:
            self.quiet(client, 0.2)
        rss = Rss(int(s.fmt("#{pid}")))
        times = []
        try:
            for _ in range(runs):
                times.append(self.feed(s, file, client))
        finally:
            peak = rss.finish()
            s.kill()
        nbytes = file.stat().st_size
        return {
            "bytes": nbytes,
            "seconds": times,
            "median_bps": nbytes / statistics.median(times),
            "best_bps": nbytes / min(times),
            "rss_kb": peak,
        }

    def resize_run(self, file: Path, widths: list[int], runs: int, size: tuple[int, int],
                   attached: bool) -> dict:
        s = self.server(size)
        client = s.attach_pty(size=size, marker=None) if attached else None
        if client is not None:
            self.quiet(client, 0.2)
        self.feed(s, file, client)
        history = int(s.fmt("#{history_size}"))
        rss = Rss(int(s.fmt("#{pid}")))
        times = []
        try:
            for _ in range(runs):
                start = time.perf_counter()
                for w in widths:
                    s.cmd("resize-window", "-t", "main", "-x", str(w))
                    if client is not None:
                        self.quiet(client, 0.1)
                times.append(time.perf_counter() - start)
        finally:
            peak = rss.finish()
            s.kill()
        return {
            "history": history,
            "widths": widths,
            "seconds": times,
            "median_s": statistics.median(times),
            "best_s": min(times),
            "rss_kb": peak,
        }


def measurements(which: str):
    if which != "attached":
        yield "detached", None
    if which != "detached":
        for size in SIZES:
            yield f"attached-{size[0]}x{size[1]}", size


def run(args) -> Path:
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    cache = out_dir / "payloads"
    names = args.scenario or list(SCENARIOS)
    result = {
        "sha": git_sha(),
        "date": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "termo": str(args.termo),
        "version": subprocess.run([str(args.termo), "-V"], capture_output=True, text=True).stdout.strip(),
        "machine": {"os": platform.system(), "release": platform.release(),
                    "arch": platform.machine(), "cpu": platform.processor() or platform.machine()},
        "runs": args.runs,
        "scale": args.scale,
        "scenarios": {},
    }
    with tempfile.TemporaryDirectory(prefix="termo-bench-") as tmp:
        bench = Bench(Path(args.termo).resolve(), Path(tmp), args.history)
        for name in names:
            spec = SCENARIOS[name]
            entry = {}
            if "payload" in spec:
                file = payload(spec["payload"], SCENARIOS[spec["payload"]]["bytes"], args.scale, cache)
                for label, size in measurements(args.measure):
                    entry[label] = bench.resize_run(file, spec["widths"], args.runs, size or (80, 24),
                                                    attached=size is not None)
                    print(f"{name:8} {label:16} {entry[label]['median_s']:.3f} s "
                          f"({entry[label]['history']} lines, rss {entry[label]['rss_kb']} kB)")
            else:
                file = payload(name, spec["bytes"], args.scale, cache)
                for label, size in measurements(args.measure):
                    entry[label] = bench.payload_run(name, file, args.runs, size or (80, 24),
                                                     attached=size is not None)
                    print(f"{name:8} {label:16} {entry[label]['median_bps'] / 1e6:8.1f} MB/s "
                          f"(rss {entry[label]['rss_kb']} kB)")
            result["scenarios"][name] = entry
    out = out_dir / f"{args.tag or result['sha']}.json"
    out.write_text(json.dumps(result, indent=1) + "\n")
    print(f"wrote {out}")
    return out


def compare(args) -> int:
    out_dir = Path(args.out)
    old = json.loads((out_dir / f"{args.compare}.json").read_text())
    new_sha = args.against or git_sha()
    new = json.loads((out_dir / f"{new_sha}.json").read_text())
    worse = []
    for name, entry in new["scenarios"].items():
        base = old["scenarios"].get(name)
        if base is None:
            continue
        for label, m in entry.items():
            b = base.get(label)
            if b is None:
                continue
            if "median_bps" in m:
                ratio = m["median_bps"] / b["median_bps"]
                line = f"{name:8} {label:16} {b['median_bps'] / 1e6:8.1f} -> {m['median_bps'] / 1e6:8.1f} MB/s  x{ratio:.3f}"
                bad = ratio < 1 - THRESHOLD
            else:
                ratio = b["median_s"] / m["median_s"]
                line = f"{name:8} {label:16} {b['median_s']:.3f} -> {m['median_s']:.3f} s  x{ratio:.3f}"
                bad = ratio < 1 - THRESHOLD
            print(line + ("  REGRESSION" if bad else ""))
            if bad:
                worse.append(f"{name}/{label}")
    if worse:
        print(f"regressed by more than {int(THRESHOLD * 100)}%: {', '.join(worse)}")
        return 1
    print(f"{old['sha']} -> {new['sha']}: no measurement regressed by more than {int(THRESHOLD * 100)}%")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("scenario", nargs="*", help=f"scenarios to run (default all: {', '.join(SCENARIOS)})")
    p.add_argument("--termo", default=str(ROOT / "build" / "termo"))
    p.add_argument("--runs", type=int, default=3)
    p.add_argument("--scale", type=float, default=1.0, help="payload size multiplier")
    p.add_argument("--history", type=int, default=50000)
    p.add_argument("--measure", choices=["all", "detached", "attached"], default="all")
    p.add_argument("--out", default=str(ROOT / "build" / "bench"))
    p.add_argument("--compare", metavar="SHA", help="compare SHA.json against --against or HEAD")
    p.add_argument("--against", metavar="SHA")
    p.add_argument("--tag", help="name of the output file instead of the git sha")
    args = p.parse_args()
    unknown = [s for s in args.scenario if s not in SCENARIOS]
    if unknown:
        p.error(f"unknown scenario: {', '.join(unknown)}")
    if args.compare:
        return compare(args)
    run(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
