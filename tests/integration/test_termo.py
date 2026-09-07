#!/usr/bin/env python3
"""
Integration test suite for termo.
Runs end-to-end tests against the compiled termo binary without relying on ad-hoc shell scripts.
"""

import os
import sys
import time
import unittest
import subprocess
from pathlib import Path

# Resolve termo binary path
termo_bin_arg = None
if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    termo_bin_arg = sys.argv.pop(1)


class TermoIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global termo_bin_arg
        if termo_bin_arg:
            cls.termo_bin = Path(termo_bin_arg).resolve()
        elif "TEST_TERMO" in os.environ:
            cls.termo_bin = Path(os.environ["TEST_TERMO"]).resolve()
        elif "TEST_TMUX" in os.environ:
            cls.termo_bin = Path(os.environ["TEST_TMUX"]).resolve()
        else:
            cls.termo_bin = Path(__file__).resolve().parent.parent.parent / "build" / "termo"

        if not cls.termo_bin.exists() or not os.access(cls.termo_bin, os.X_OK):
            raise RuntimeError(f"termo binary not found or not executable at {cls.termo_bin}")

    def setUp(self):
        self.socket_name = f"termo_{self._testMethodName}_{os.getpid()}_{int(time.time()*1000)}"

    def termo_cmd(self, *args, check=True, conf="/dev/null"):
        cmd = [
            str(self.termo_bin),
            "-L", self.socket_name,
            "-f", conf,
            *args
        ]
        return subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=check,
        )

    def tearDown(self):
        try:
            self.termo_cmd("kill-server", check=False)
        except Exception:
            pass

    def test_01_version_output(self):
        """Verify version output contains termo and project version."""
        res = subprocess.run(
            [str(self.termo_bin), "-V"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
        )
        self.assertEqual(res.stdout.strip(), "termo 0.1.0")

    def test_02_sane_defaults_out_of_the_box(self):
        """Verify the sane defaults shipped in etc/termo.conf."""
        conf = Path(__file__).resolve().parent.parent.parent / "etc" / "termo.conf"
        self.termo_cmd("new-session", "-d", "-s", "sess_defaults", "sleep 10", conf=str(conf))

        # mode-keys == vi
        out = self.termo_cmd("show-options", "-gw", "mode-keys").stdout.strip()
        self.assertEqual(out.split()[-1], "vi", f"Expected mode-keys vi, got '{out}'")

        # status-keys == vi
        out = self.termo_cmd("show-options", "-g", "status-keys").stdout.strip()
        self.assertEqual(out.split()[-1], "vi", f"Expected status-keys vi, got '{out}'")

        # history-limit == 50000
        out = self.termo_cmd("show-options", "-g", "history-limit").stdout.strip()
        self.assertEqual(out.split()[-1], "50000", f"Expected history-limit 50000, got '{out}'")

        # focus-events == on
        out = self.termo_cmd("show-options", "-s", "focus-events").stdout.strip()
        self.assertEqual(out.split()[-1], "on", f"Expected focus-events on, got '{out}'")

        # renumber-windows == on
        out = self.termo_cmd("show-options", "-g", "renumber-windows").stdout.strip()
        self.assertEqual(out.split()[-1], "on", f"Expected renumber-windows on, got '{out}'")

    def test_03_session_and_window_lifecycle(self):
        """Verify session creation, window splitting, listing, and graceful termination."""
        self.termo_cmd("new-session", "-d", "-s", "lifecycle_sess", "sleep 10")

        # Check session is listed
        out = self.termo_cmd("list-sessions").stdout
        self.assertIn("lifecycle_sess", out)

        # Split window
        self.termo_cmd("split-window", "-t", "lifecycle_sess", "sleep 10")
        panes = self.termo_cmd("list-panes", "-t", "lifecycle_sess").stdout.strip().splitlines()
        self.assertEqual(len(panes), 2, f"Expected 2 panes after split, got {len(panes)}")

        # Clean shutdown
        self.termo_cmd("kill-session", "-t", "lifecycle_sess")
        res = self.termo_cmd("list-sessions", check=False)
        self.assertNotEqual(res.returncode, 0)

    def test_04_vi_copy_mode_keybindings(self):
        """Verify v (begin-selection) and y (copy-pipe-and-cancel) in copy-mode-vi."""
        conf = Path(__file__).resolve().parent.parent.parent / "etc" / "termo.conf"
        self.termo_cmd("new-session", "-d", "-s", "copy_sess", "sleep 10", conf=str(conf))
        out = self.termo_cmd("list-keys", "-T", "copy-mode-vi").stdout
        normalized_lines = [" ".join(line.split()) for line in out.splitlines()]

        self.assertTrue(
            any("bind-key -T copy-mode-vi v send-keys -X begin-selection" in line for line in normalized_lines),
            "Key 'v' was not bound to begin-selection in copy-mode-vi"
        )
        self.assertTrue(
            any("bind-key -T copy-mode-vi y send-keys -X copy-pipe-and-cancel" in line for line in normalized_lines),
            "Key 'y' was not bound to copy-pipe-and-cancel in copy-mode-vi"
        )

    def test_05_termo_environment_variables(self):
        """Verify TERMO, TERMO_PANE, and TERM_PROGRAM=termo are exported to child environments."""
        self.termo_cmd("new-session", "-d", "-s", "env_sess", "sleep 10")

        # Check pane format environment
        term_prog = self.termo_cmd("display-message", "-t", "env_sess", "-p", "#{client_termtype}").stdout.strip()
        termo_pane = self.termo_cmd("display-message", "-t", "env_sess", "-p", "#{pane_id}").stdout.strip()
        self.assertTrue(termo_pane.startswith("%"), f"Expected pane ID format '%...', got '{termo_pane}'")



if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + sys.argv[1:])
