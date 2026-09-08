#!/usr/bin/env python3
"""Drive the Lua UI through a real terminal: a client attached on a pty
presses keys bound in lua_ui_init.lua, and the server records what the
handlers saw in user options."""

import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import sys
import termios
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FIXTURE = HERE / "lua_ui_init.lua"

termo_bin = None
if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    termo_bin = Path(sys.argv.pop(1)).resolve()
else:
    termo_bin = ROOT / "build" / "termo"

PREFIX = b"\x02"
KEYS = {
    "F5": b"\x1b[15~",
    "F6": b"\x1b[17~",
    "F7": b"\x1b[18~",
    "F8": b"\x1b[19~",
    "F9": b"\x1b[20~",
    "F10": b"\x1b[21~",
}


class Terminal:
    """An 80x24 pty with a termo client attached in it."""

    def __init__(self, sock, env):
        pid, fd = pty.fork()
        if pid == 0:
            os.environ.update(env)
            os.environ["TERM"] = "xterm"
            os.execv(str(termo_bin), [str(termo_bin), "-L", sock, "attach"])
        self.pid, self.fd = pid, fd
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        self.buf = b""

    def read_some(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        try:
            data = os.read(self.fd, 65536)
        except OSError:
            return False
        self.buf += data
        return bool(data)

    def wait_for(self, pattern, timeout=8.0):
        deadline = time.monotonic() + timeout
        regex = re.compile(pattern.encode())
        while True:
            if regex.search(self.buf):
                return True
            left = deadline - time.monotonic()
            if left <= 0:
                tail = self.buf[-600:].decode("utf-8", "replace")
                raise AssertionError(f"{pattern!r} not seen; tail: {tail!r}")
            self.read_some(min(left, 0.2))

    def send(self, *chunks):
        for chunk in chunks:
            os.write(self.fd, chunk)
            time.sleep(0.05)

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass


class LuaUITest(unittest.TestCase):
    def setUp(self):
        self.sock = f"luaui_{os.getpid()}_{int(time.time() * 1000)}"
        self.env = dict(os.environ)
        self.env["TERMO_RUNTIME"] = str(ROOT / "runtime")
        self.env["SHELL"] = "/bin/sh"
        self.env.pop("TMUX", None)
        self.env.pop("TERMO", None)
        self.run_termo("-f", "/dev/null", "-f", str(FIXTURE), "new-session",
                       "-d", "-x", "80", "-y", "24")
        self.assertEqual(self.option("@ready"), "yes")
        self.term = Terminal(self.sock, self.env)
        # The status line carries the Lua variable: drawn means attached.
        self.term.wait_for("lua-status-ok")

    def tearDown(self):
        self.run_termo("kill-server", check=False)
        self.term.close()

    def run_termo(self, *args, check=True):
        return subprocess.run([str(termo_bin), "-L", self.sock, *args],
                              env=self.env, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True, check=check)

    def option(self, name):
        r = self.run_termo("show", "-gv", name, check=False)
        return r.stdout.strip() if r.returncode == 0 else None

    def wait_option(self, name, timeout=8.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = self.option(name)
            if value:
                return value
            time.sleep(0.1)
        return self.option(name)

    def test_key_handler_gets_the_event(self):
        self.term.send(PREFIX, KEYS["F5"])
        self.assertEqual(self.wait_option("@key"), "F5/prefix/true")

    def test_menu_runs_the_chosen_function(self):
        self.term.send(PREFIX, KEYS["F6"])
        self.term.wait_for("Pick one")
        self.term.wait_for("Third")
        self.term.send(b"b")
        self.assertEqual(self.wait_option("@picked"), "two/2/b")
        self.assertIsNone(self.option("@menu_closed"))

    def test_menu_runs_a_command_item_and_closes(self):
        self.term.send(PREFIX, KEYS["F6"])
        self.term.wait_for("Third")
        self.term.send(b"c")
        self.assertEqual(self.wait_option("@picked"), "three")
        self.term.send(PREFIX, KEYS["F6"])
        self.term.wait_for("Second")
        self.term.send(b"q")
        self.assertEqual(self.wait_option("@menu_closed"), "yes")

    def test_prompt_delivers_the_text(self):
        self.term.send(PREFIX, KEYS["F7"])
        self.term.wait_for("Name:")
        self.term.send(b"hello\r")
        self.assertEqual(self.wait_option("@prompt"), "hello/true")

    def test_prompt_cancelled_gives_nil(self):
        self.term.send(PREFIX, KEYS["F7"])
        self.term.wait_for("Name:")
        self.term.send(b"\x1b")
        self.assertEqual(self.wait_option("@prompt"), "nil/true")

    def test_popup_shows_output_and_reports_exit(self):
        self.term.send(PREFIX, KEYS["F8"])
        self.term.wait_for("popup-ok")
        self.assertEqual(self.wait_option("@popup"), "0")

    def test_message_reaches_the_status_line(self):
        self.term.send(PREFIX, KEYS["F9"])
        self.term.wait_for("hello from lua")

    def test_palette_filters_and_runs(self):
        self.term.send(PREFIX, KEYS["F10"])
        self.term.wait_for("\r\n>")
        self.term.send(b"mark")
        self.term.wait_for("Mark spot")
        self.term.send(b"\r")
        self.assertEqual(self.wait_option("@palette"), "marked")


if __name__ == "__main__":
    unittest.main(verbosity=2)
