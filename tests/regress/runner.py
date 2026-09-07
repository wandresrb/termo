#!/usr/bin/env python3
"""Run the regression scripts in this directory against a termo binary."""

import argparse
import os
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REGRESS_DIR = Path(__file__).resolve().parent


def kill_leftovers(script_pid):
    """Scripts name their sockets -LtestA$$/-LtestB$$; kill servers they left."""
    ps = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True,
                        text=True).stdout
    killed = 0
    for line in ps.splitlines():
        pid, _, cmd = line.strip().partition(" ")
        if f"-LtestA{script_pid}" in cmd or f"-LtestB{script_pid}" in cmd:
            try:
                os.kill(int(pid), signal.SIGKILL)
                killed += 1
            except (OSError, ValueError):
                pass
    return killed


def run_one(script, env, timeout):
    start = time.time()
    proc = subprocess.Popen(["sh", script], cwd=REGRESS_DIR, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    try:
        out, _ = proc.communicate(timeout=timeout)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
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
    ap.add_argument("--timeout", type=int, default=300, help="seconds per script")
    ap.add_argument("--log-dir", default=str(REGRESS_DIR / "logs"),
                    help="where to write <script>.log for failures")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_intermixed_args()

    termo = Path(args.termo_bin).resolve()
    if not os.access(termo, os.X_OK):
        print(f"not executable: {termo}", file=sys.stderr)
        return 2

    if args.tests:
        scripts = [Path(t).name if t.endswith(".sh") else Path(t).name + ".sh" for t in args.tests]
        missing = [s for s in scripts if not (REGRESS_DIR / s).exists()]
        if missing:
            print(f"no such test: {' '.join(missing)}", file=sys.stderr)
            return 2
    else:
        scripts = sorted(p.name for p in REGRESS_DIR.glob("*.sh"))

    env = os.environ.copy()
    env["TEST_TERMO"] = str(termo)
    env["TEST_TMUX"] = str(termo)
    env["LC_CTYPE"] = "C.UTF-8"
    env["MallocNanoZone"] = "0"
    # A user shell with a title-setting prompt renames windows under the tests.
    env["SHELL"] = "/bin/sh"

    xfail = {}
    xfail_file = REGRESS_DIR / "xfail"
    if xfail_file.exists():
        for line in xfail_file.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                name, _, why = line.partition(" ")
                xfail[name] = why

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    for old in log_dir.glob("*.log"):
        old.unlink()

    print(f"running {len(scripts)} regression tests, -j{args.jobs}, {termo}")
    failed = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for script, rc, out, elapsed in pool.map(
                lambda s: run_one(s, env, args.timeout), scripts):
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
