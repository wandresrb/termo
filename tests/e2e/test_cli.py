import re
import subprocess

import pytest

from termo import ROOT, expect_screen

CONF = ROOT / "etc/termo.conf"


def test_version_prints_project_version(termo_bin, project_version):
    r = subprocess.run([str(termo_bin), "-V"], capture_output=True, text=True, check=True)
    assert r.stdout == f"termo {project_version}\n"


def test_termo_conf_defaults(server):
    server.start(conf=CONF)
    assert server.option("mode-keys") == "vi"
    assert server.option("status-keys") == "vi"
    assert server.option("history-limit") == "50000"
    assert server.option("focus-events") == "on"
    assert server.option("renumber-windows") == "on"
    assert server.option("mouse") == "on"
    assert server.option("escape-time") == "10"
    assert server.option("default-terminal") == "tmux-256color"


def test_copy_mode_vi_bindings_from_conf(server):
    server.start(conf=CONF)
    keys = re.sub(r"\s+", " ", server.out("list-keys", "-T", "copy-mode-vi"))
    assert "bind-key -T copy-mode-vi v send-keys -X begin-selection" in keys
    assert "bind-key -T copy-mode-vi y send-keys -X copy-pipe-and-cancel" in keys


def test_pane_environment_has_termo_variables(server):
    server.start(command="sh -c 'echo $TERMO; echo $TERMO_PANE; echo $TERM_PROGRAM; echo $TMUX;"
                         " exec sleep 100'")
    rows = expect_screen(server, None,
                         [r"{MATCH:/\S+,\d+,\d+}", r"{MATCH:%\d+}", "termo", r"{MATCH:/\S+,\d+,\d+}"])
    assert rows[0] == rows[3]
    assert rows[1] == server.fmt("#{pane_id}")


def test_no_server_running_is_an_error(server):
    r = server.cmd("list-sessions", check=False)
    assert r.returncode == 1
    assert "error connecting to" in r.stderr


@pytest.mark.nolua
def test_run_lua_is_unknown_without_luajit(server):
    server.start()
    r = server.cmd("run-lua", "return 1", check=False)
    assert r.returncode != 0
    assert "unknown command: run-lua" in r.stderr
