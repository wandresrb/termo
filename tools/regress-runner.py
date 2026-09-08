#!/usr/bin/env python3
"""Run upstream tmux's regress/ scripts against a termo binary. `just
upstream-regress` fetches them into build/upstream-regress/."""

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REGRESS_DIR = ROOT / "build/upstream-regress/regress"


def kill_leftovers(script_pid):
    """Scripts name their sockets -LtestA$$/-LtestB$$; kill servers they left."""
    ps = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True,
                        text=True).stdout
    # Exact socket name: pid 8893 must not match pid 88938; -1/-2 suffixes are ok.
    mine = re.compile(rf"-Ltest[AB]{script_pid}(?:-\d+)?(?=\s|$)")
    killed = 0
    for line in ps.splitlines():
        pid, _, cmd = line.strip().partition(" ")
        if mine.search(cmd):
            try:
                os.kill(int(pid), signal.SIGKILL)
                killed += 1
            except (OSError, ValueError):
                pass
    return killed


def run_one(script, regress_dir, env, timeout):
    start = time.time()
    proc = subprocess.Popen(["sh", script], cwd=regress_dir, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    try:
        out, _ = proc.communicate(timeout=timeout)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
        out += f"\n[runner] timeout after {timeout}s\n"
        rc = "timeout"
    leaked = kill_leftovers(proc.pid)
    if leaked:
        out += f"\n[runner] killed {leaked} server(s) the script left running\n"
    return script, rc, out, time.time() - start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("termo_bin")
    ap.add_argument("tests", nargs="*", help="scripts to run (default: all *.sh)")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--timeout", type=int, default=600, help="seconds per script")
    ap.add_argument("--dir", default=str(REGRESS_DIR), help="directory with the *.sh scripts")
    ap.add_argument("--xfail", default=str(ROOT / "tools/regress-xfail"),
                    help="scripts expected to fail, one `name reason` per line")
    ap.add_argument("--log-dir", default=None,
                    help="where to write <script>.log for failures (default: <dir>/../logs)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_intermixed_args()

    termo = Path(args.termo_bin).resolve()
    if not os.access(termo, os.X_OK):
        print(f"not executable: {termo}", file=sys.stderr)
        return 2
    regress_dir = Path(args.dir).resolve()
    if not regress_dir.is_dir():
        print(f"no such directory: {regress_dir} (run `just upstream-regress`)", file=sys.stderr)
        return 2

    if args.tests:
        scripts = [Path(t).name if t.endswith(".sh") else Path(t).name + ".sh" for t in args.tests]
        missing = [s for s in scripts if not (regress_dir / s).exists()]
        if missing:
            print(f"no such test: {' '.join(missing)}", file=sys.stderr)
            return 2
    else:
        scripts = sorted(p.name for p in regress_dir.glob("*.sh"))

    env = os.environ.copy()
    env["TEST_TERMO"] = str(termo)
    env["TEST_TMUX"] = str(termo)
    env["LC_CTYPE"] = "C.UTF-8"
    env["MallocNanoZone"] = "0"
    # A user shell with a title-setting prompt renames windows under the tests.
    env["SHELL"] = "/bin/sh"

    xfail = {}
    xfail_file = Path(args.xfail)
    if xfail_file.exists():
        for line in xfail_file.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                name, _, why = line.partition(" ")
                xfail[name] = why

    log_dir = Path(args.log_dir) if args.log_dir else regress_dir.parent / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    for old in log_dir.glob("*.log"):
        old.unlink()

    print(f"running {len(scripts)} regression tests, -j{args.jobs}, {termo}")
    failed = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for script, rc, out, elapsed in pool.map(
                lambda s: run_one(s, regress_dir, env, args.timeout), scripts):
            if rc == 0:
                tag = "XPASS" if script in xfail else "ok"
                note = "  leaked a server" if "[runner] killed" in out else ""
                print(f"  {tag:<7} {script:<40} {elapsed:6.2f}s{note}")
                continue
            status = "timeout" if rc == "timeout" else f"exit {rc}"
            if script in xfail:
                print(f"  xfail   {script:<40} {elapsed:6.2f}s  {xfail[script]}")
                continue
            print(f"  FAIL    {script:<40} {elapsed:6.2f}s  {status}")
            (log_dir / f"{script}.log").write_text(out)
            failed.append((script, out))
            if args.verbose:
                print(out[-2000:])

    print(f"\n{len(scripts) - len(failed)} passed, {len(failed)} failed")
    for script, out in failed:
        tail = "\n    ".join(out.strip().splitlines()[-5:])
        print(f"  {script}  ({log_dir / (script + '.log')})\n    {tail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
