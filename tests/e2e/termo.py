"""Drive a termo server from outside: commands over its socket, screens through
capture-pane, notifications through control mode, keys through a pty. Every
wait is a poll with a deadline; nothing sleeps for a fixed time."""

from __future__ import annotations

import difflib
import fcntl
import glob
import os
import pty
import re
import select
import signal
import struct
import subprocess
import termios
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from queue import Empty, Queue
from pathlib import Path
from typing import Callable, Optional, Sequence, TypeVar

ROOT = Path(__file__).resolve().parents[2]

POLL = 0.05
SCALE = float(os.environ.get("TERMO_E2E_TIMEOUT_SCALE", "1"))
DEFAULT_TIMEOUT = 5.0

DROP_ENV = ("TMUX", "TERMO", "TMUX_PANE", "TERMO_PANE", "TMUX_TMPDIR",
            "EDITOR", "VISUAL", "LC_ALL", "LANG", "TERM_PROGRAM")

T = TypeVar("T")


class ExpectTimeout(AssertionError):
    pass


def scaled(timeout: Optional[float]) -> float:
    return (DEFAULT_TIMEOUT if timeout is None else timeout) * SCALE


def expect(pred: Callable[[], Optional[T]], *, timeout: Optional[float] = None,
           what: str, describe: Optional[Callable[[], str]] = None) -> T:
    deadline = time.monotonic() + scaled(timeout)
    while True:
        value = pred()
        if value:
            return value
        if time.monotonic() >= deadline:
            detail = f"\n{describe()}" if describe else ""
            raise ExpectTimeout(f"timed out after {scaled(timeout):.1f}s waiting for {what}{detail}")
        time.sleep(POLL)


def sanitizer_reports(since: float) -> list[Path]:
    found = []
    for pattern in ("/tmp/termo-asan.*", "/tmp/termo-ubsan.*"):
        for p in glob.glob(pattern):
            if os.path.getmtime(p) >= since:
                found.append(Path(p))
    return sorted(found)


def reap_leftovers(name: str) -> int:
    """SIGKILL servers and clients still holding this socket name. Linux and macOS
    keep argv (`-L name`); the BSDs rewrite it to `termo: server (/path/name)`."""
    ps = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True,
                        text=True).stdout
    mine = re.compile(rf"(?:-L ?{re.escape(name)}|/{re.escape(name)}\)?)(?=\s|$)")
    killed = 0
    for line in ps.splitlines():
        pid, _, cmd = line.strip().partition(" ")
        if mine.search(cmd) and int(pid) != os.getpid():
            try:
                os.kill(int(pid), signal.SIGKILL)
                killed += 1
            except (OSError, ValueError):
                pass
    return killed


class Server:
    def __init__(self, binary: Path, name: str, *, tmpdir: Path, keep: bool = False):
        self.binary = Path(binary)
        self.name = name
        self.tmpdir = Path(tmpdir)
        self.keep = keep
        self.started_at = time.time()
        self.children: list[subprocess.Popen] = []
        self.clients: list = []
        self.reports: list[Path] = []
        xdg = self.tmpdir / "xdg"
        xdg.mkdir(parents=True, exist_ok=True)
        self.env = {k: v for k, v in os.environ.items() if k not in DROP_ENV}
        self.env.update({
            "SHELL": "/bin/sh",
            "LC_CTYPE": "C.UTF-8",
            "TERM": "xterm",
            "MallocNanoZone": "0",
            "TERMO_RUNTIME": os.environ.get("TERMO_RUNTIME", str(ROOT / "runtime")),
            "XDG_CONFIG_HOME": str(xdg),
        })

    def argv(self, *args: str) -> list[str]:
        return [str(self.binary), "-L", self.name, *args]

    def start(self, *args: str, conf: os.PathLike | str | None = "/dev/null",
              lua_init: os.PathLike | str | None = None,
              size: tuple[int, int] = (80, 24), session: str = "main",
              command: Optional[str] = None) -> "Server":
        cmd = [] if conf is None else ["-f", str(conf)]
        if lua_init is not None:
            cmd += ["-f", str(lua_init)]
        if os.environ.get("TERMO_E2E_VERBOSE"):
            cmd += ["-vv"]
        cmd += ["new-session", "-d", "-s", session, "-x", str(size[0]), "-y", str(size[1]),
                *args]
        if command is not None:
            cmd.append(command)
        self.started_at = time.time()
        self.cmd(*cmd)
        return self

    def cmd(self, *args: str, check: bool = True, timeout: Optional[float] = None,
            input: Optional[str] = None) -> subprocess.CompletedProcess:
        r = subprocess.run(self.argv(*args), env=self.env, cwd=self.tmpdir,
                           capture_output=True, text=True, input=input,
                           timeout=scaled(10 if timeout is None else timeout))
        if check and r.returncode != 0:
            raise AssertionError(
                f"termo {' '.join(args)} exited {r.returncode}\n"
                f"stdout: {r.stdout!r}\nstderr: {r.stderr!r}")
        return r

    def out(self, *args: str) -> str:
        return self.cmd(*args).stdout.rstrip("\n")

    def fmt(self, fmt: str, target: Optional[str] = None) -> str:
        args = ["display-message", "-p"]
        if target is not None:
            args += ["-t", target]
        return self.out(*args, fmt)

    def option(self, name: str, scope: str = "g",
               target: Optional[str] = None) -> Optional[str]:
        args = ["show-options", f"-{scope}qv"]
        if target is not None:
            args += ["-t", target]
        r = self.cmd(*args, name, check=False)
        if r.returncode != 0 or r.stdout == "":
            return None
        return r.stdout.rstrip("\n")

    def capture(self, target: Optional[str] = None, *, escapes: bool = False,
                start: Optional[int] = None, end: Optional[int] = None,
                join: bool = False) -> list[str]:
        args = ["capture-pane", "-p"]
        if escapes:
            args.append("-e")
        if join:
            args.append("-J")
        if start is not None:
            args += ["-S", str(start)]
        if end is not None:
            args += ["-E", str(end)]
        if target is not None:
            args += ["-t", target]
        return [row.rstrip() for row in self.cmd(*args).stdout.split("\n")[:-1]]

    def pane_ids(self, target: Optional[str] = None) -> list[str]:
        args = ["list-panes", "-F", "#{pane_id}"]
        if target is not None:
            args += ["-t", target]
        return self.out(*args).split()

    def alive(self) -> bool:
        return self.cmd("display-message", "-p", "ok", check=False).returncode == 0

    def popen(self, *args: str, **kw) -> subprocess.Popen:
        kw.setdefault("env", self.env)
        kw.setdefault("stdout", subprocess.PIPE)
        kw.setdefault("stderr", subprocess.STDOUT)
        kw.setdefault("text", True)
        p = subprocess.Popen(self.argv(*args), **kw)
        self.children.append(p)
        return p

    def attach_control(self, target: Optional[str] = None, *, output: bool = False,
                       flags: Sequence[str] = ()) -> "Control":
        argv = ["attach-session"] + (["-t", target] if target else [])
        for flag in ([] if output else ["no-output"]) + list(flags):
            argv += ["-f", flag]
        client = Control(self, argv)
        self.clients.append(client)
        client.ready()
        return client

    def nest(self, inner: "Server", size: tuple[int, int] = (80, 24)) -> str:
        """Start this server with one pane running a client of `inner`, status
        off, so capture() of that pane is the inner client's whole screen."""
        conf = self.tmpdir / f"{self.name}-outer.conf"
        conf.write_text("set -g status off\n")
        self.start(conf=conf, size=size,
                   command=f"{self.binary} -L {inner.name} attach-session")
        expect(lambda: inner.out("list-clients", "-F", "#{client_name}") != "",
               what="the nested client to attach")
        return self.pane_ids()[0]

    def attach_pty(self, target: Optional[str] = None, *, size: tuple[int, int] = (80, 24),
                   term: str = "xterm", marker: Optional[str] = "e2e-ready") -> "Pty":
        argv = ["attach-session"] + (["-t", target] if target else [])
        client = Pty(self, argv, size=size, term=term)
        self.clients.append(client)
        expect(lambda: str(client.pid) in self.out("list-clients", "-F", "#{client_pid}").split(),
               what="the pty client to attach")
        if marker is not None:
            client.wait_for(marker)
        return client

    def kill(self, failed: bool = False) -> None:
        if self.keep and failed:
            print(f"\nserver kept: {self.binary} -L {self.name} attach")
            return
        for c in self.clients:
            c.close(timeout=2)
        self.cmd("kill-server", check=False, timeout=10)
        for p in self.children:
            if p.poll() is None:
                p.kill()
            try:
                p.wait(timeout=scaled(2))
            except subprocess.TimeoutExpired:
                pass
        reap_leftovers(self.name)
        self.reports = sanitizer_reports(self.started_at)


def expect_fmt(server: Server, fmt: str, want, *, target: Optional[str] = None,
               timeout: Optional[float] = None) -> str:
    last = {"value": None}

    def check():
        got = server.fmt(fmt, target)
        last["value"] = got
        if isinstance(want, re.Pattern):
            return got if want.fullmatch(got) else None
        return got if got == want else None

    return expect(check, timeout=timeout, what=f"{fmt} == {want!r}",
                  describe=lambda: f"last value: {last['value']!r}")


WILD = "*"
MATCH = re.compile(r"^\{MATCH:(.*)\}$")


def _row_ok(want: str, got: str) -> bool:
    if want == WILD:
        return True
    m = MATCH.match(want)
    if m:
        return re.fullmatch(m.group(1), got) is not None
    return want.rstrip() == got


def expect_screen(server: Server, target: Optional[str], want: Sequence[str], *,
                  timeout: Optional[float] = None, escapes: bool = False,
                  start: Optional[int] = None, end: Optional[int] = None) -> list[str]:
    want = list(want)
    last: list[str] = []

    def check():
        nonlocal last
        got = server.capture(target, escapes=escapes, start=start, end=end)
        last = got
        if len(got) < len(want):
            return None
        if not all(_row_ok(w, g) for w, g in zip(want, got)):
            return None
        if any(g.strip() for g in got[len(want):]):
            return None
        return got

    def describe():
        return "\n".join(difflib.unified_diff(want, last, "want", "got", lineterm=""))

    return expect(check, timeout=timeout, what=f"screen of {target or 'the active pane'}",
                  describe=describe)


KEYS = {
    "Enter": b"\r", "Escape": b"\x1b", "Tab": b"\t", "BSpace": b"\x7f", "Space": b" ",
    "Up": b"\x1b[A", "Down": b"\x1b[B", "Right": b"\x1b[C", "Left": b"\x1b[D",
    "Home": b"\x1b[H", "End": b"\x1b[F", "PPage": b"\x1b[5~", "NPage": b"\x1b[6~",
    "IC": b"\x1b[2~", "DC": b"\x1b[3~",
    "F1": b"\x1bOP", "F2": b"\x1bOQ", "F3": b"\x1bOR", "F4": b"\x1bOS",
    "F5": b"\x1b[15~", "F6": b"\x1b[17~", "F7": b"\x1b[18~", "F8": b"\x1b[19~",
    "F9": b"\x1b[20~", "F10": b"\x1b[21~", "F11": b"\x1b[23~", "F12": b"\x1b[24~",
}


def key_bytes(key: str) -> bytes:
    if key in KEYS:
        return KEYS[key]
    if len(key) == 3 and key[1] == "-" and key[0] in "CM":
        rest = key[2:]
        if key[0] == "C":
            return bytes([ord(rest.lower()) & 0x1f])
        return b"\x1b" + rest.encode()
    return key.encode()


class Pty:
    """A termo client on a pseudo-terminal: keys go in as bytes, the raw output
    is searched with a regex. Screen content is asserted through capture-pane on
    the server, not here."""

    def __init__(self, server: Server, argv: list[str], *, size: tuple[int, int] = (80, 24),
                 term: str = "xterm"):
        env = dict(server.env)
        env["TERM"] = term
        pid, fd = pty.fork()
        if pid == 0:
            os.execve(str(server.binary), server.argv(*argv), env)
        self.pid, self.fd = pid, fd
        self.resize(*size)
        self.buf = b""

    def resize(self, cols: int, rows: int) -> None:
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def send_keys(self, *keys: str) -> None:
        for key in keys:
            os.write(self.fd, key_bytes(key))

    def send_text(self, text: str) -> None:
        os.write(self.fd, text.encode())

    def read_some(self, timeout: float) -> bool:
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        try:
            data = os.read(self.fd, 65536)
        except OSError:
            return False
        self.buf += data
        return bool(data)

    def wait_for(self, pattern, *, timeout: Optional[float] = None) -> re.Match:
        """Match against output received since the previous match, so a second
        menu is not satisfied by the bytes of the first one."""
        regex = re.compile(pattern.encode() if isinstance(pattern, str) else pattern)
        deadline = time.monotonic() + scaled(timeout)
        while True:
            m = regex.search(self.buf)
            if m:
                self.buf = self.buf[m.end():]
                return m
            left = deadline - time.monotonic()
            if left <= 0:
                tail = self.buf[-600:].decode("utf-8", "replace")
                raise ExpectTimeout(f"{pattern!r} not seen on the pty; tail: {tail!r}")
            self.read_some(min(left, POLL))

    def close(self, timeout: Optional[float] = None) -> None:
        try:
            os.close(self.fd)
        except OSError:
            pass
        deadline = time.monotonic() + scaled(timeout)
        while True:
            try:
                pid, _ = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                return
            if pid:
                return
            if time.monotonic() >= deadline:
                os.kill(self.pid, signal.SIGKILL)
                os.waitpid(self.pid, 0)
                return
            time.sleep(POLL)


GUARD = re.compile(r"^%(begin|end|error) (\d+) (\d+) (\d+)$")
OCTAL = re.compile(r"\\([0-7]{3})")


@dataclass
class Block:
    time: int
    number: int
    flags: int
    lines: list = field(default_factory=list)
    kind: str = ""
    extra: list = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.kind == "end"

    @property
    def text(self) -> str:
        return "\n".join(self.lines)


@dataclass
class Note:
    seq: int
    kind: str
    args: list
    raw: str


class Control:
    """A control-mode client. Block numbers are a server-wide counter, so a
    command is matched to its block by position: run() follows it with a
    display-message token and takes every block up to the token's."""

    def __init__(self, server: Server, argv: list):
        self.server = server
        self.proc = subprocess.Popen(server.argv("-C", *argv), env=server.env,
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, bufsize=0)
        self._blocks: Queue = Queue()
        self._notes: list = []
        self._cond = threading.Condition()
        self._log: deque = deque(maxlen=2000)
        self._eof = threading.Event()
        self._lock = threading.Lock()
        self._mark = 0
        self._tokens = 0
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self) -> None:
        stack: list = []
        for raw in self.proc.stdout:
            line = raw.decode("utf-8", "backslashreplace").rstrip("\r\n")
            self._log.append(line)
            m = GUARD.match(line)
            if m and m.group(1) == "begin":
                stack.append(Block(int(m[2]), int(m[3]), int(m[4])))
                continue
            if m:
                key = (int(m[2]), int(m[3]), int(m[4]))
                idx = next((i for i in range(len(stack) - 1, -1, -1)
                            if (stack[i].time, stack[i].number, stack[i].flags) == key), None)
                if idx is None:
                    if stack:
                        stack[-1].lines.append(line)
                    else:
                        self._note(line)
                    continue
                blk = stack[idx]
                for fake in stack[idx + 1:]:
                    blk.lines.append(f"%begin {fake.time} {fake.number} {fake.flags}")
                    blk.lines.extend(fake.lines)
                del stack[idx:]
                blk.kind = m.group(1)
                self._blocks.put(blk)
                continue
            if stack:
                stack[-1].lines.append(line)
                continue
            self._note(line)
        self._eof.set()
        with self._cond:
            self._cond.notify_all()

    def _note(self, line: str) -> None:
        kind, _, rest = line.partition(" ")
        if kind == "%output":
            pane, _, payload = rest.partition(" ")
            args = [pane, OCTAL.sub(lambda m: chr(int(m.group(1), 8)), payload)]
        elif kind in ("%subscription-changed", "%extended-output"):
            head, _, value = rest.partition(" : ")
            args = head.split() + [value]
        else:
            args = rest.split()
        with self._cond:
            self._notes.append(Note(len(self._notes), kind, args, line))
            self._cond.notify_all()

    def tail(self, n: int = 30) -> str:
        return "\n".join(list(self._log)[-n:])

    def _next_block(self, timeout: Optional[float]) -> Block:
        deadline = time.monotonic() + scaled(timeout)
        while True:
            try:
                return self._blocks.get(timeout=POLL)
            except Empty:
                if self._eof.is_set() and self._blocks.empty():
                    raise ExpectTimeout(f"control client exited\n{self.tail()}")
                if time.monotonic() >= deadline:
                    raise ExpectTimeout(f"no block within {scaled(timeout):.1f}s\n{self.tail()}")

    def ready(self, timeout: Optional[float] = None) -> None:
        self._next_block(timeout)
        self.expect("%session-changed", timeout=timeout)

    def send(self, line: str) -> None:
        self.proc.stdin.write((line + "\n").encode())
        self.proc.stdin.flush()

    def run(self, command: str, *, timeout: Optional[float] = None) -> Block:
        assert command.strip() and "\n" not in command
        with self._lock:
            self._tokens += 1
            token = f"e2e-token-{self._tokens}"
            with self._cond:
                mark = len(self._notes)
            self.send(command)
            self.send(f"display-message -p {token}")
        first = self._next_block(timeout)
        blocks = [first]
        while blocks[-1].lines != [token]:
            blocks.append(self._next_block(timeout))
        first.extra = blocks[1:-1]
        self._mark = mark
        return first

    def mark(self) -> int:
        return len(self._notes)

    def notes(self, since: int = 0, kind: Optional[str] = None) -> list:
        with self._cond:
            found = self._notes[since:]
        return [n for n in found if kind is None or n.kind == kind]

    def expect(self, kind: str, pred: Optional[Callable[[Note], bool]] = None, *,
               since: Optional[int] = None, timeout: Optional[float] = None) -> Note:
        start = self._mark if since is None else since
        deadline = time.monotonic() + scaled(timeout)
        with self._cond:
            while True:
                for n in self._notes[start:]:
                    if n.kind == kind and (pred is None or pred(n)):
                        self._mark = n.seq + 1
                        return n
                if self._eof.is_set():
                    raise ExpectTimeout(f"control client exited before {kind}\n{self.tail()}")
                left = deadline - time.monotonic()
                if left <= 0:
                    raise ExpectTimeout(f"no {kind} within {scaled(timeout):.1f}s\n{self.tail()}")
                self._cond.wait(min(left, POLL))

    def expect_output(self, pane: str, text: str, *, since: Optional[int] = None,
                      timeout: Optional[float] = None) -> str:
        start = self._mark if since is None else since
        deadline = time.monotonic() + scaled(timeout)
        with self._cond:
            while True:
                data = "".join(n.args[1] for n in self._notes[start:]
                               if n.kind == "%output" and n.args[0] == pane)
                if text in data:
                    return data
                left = deadline - time.monotonic()
                if left <= 0 or self._eof.is_set():
                    raise ExpectTimeout(f"{text!r} not in output of {pane}: {data!r}\n{self.tail()}")
                self._cond.wait(min(left, POLL))

    def client_name(self) -> str:
        for line in self.server.out("list-clients", "-F",
                                    "#{client_name} #{client_control_mode}").splitlines():
            name, mode = line.split()
            if mode == "1":
                return name
        raise AssertionError("no control client listed")

    def flags(self) -> set:
        return set(self.server.fmt("#{client_flags}", self.client_name()).split(","))

    def close(self, timeout: Optional[float] = None) -> int:
        if self.proc.poll() is None:
            try:
                self.send("")
            except (BrokenPipeError, OSError):
                pass
            try:
                self.proc.wait(timeout=scaled(timeout))
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        return self.proc.returncode
