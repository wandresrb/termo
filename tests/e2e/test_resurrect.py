import os
import re

from termo import ROOT, expect, expect_fmt


FIXTURES = ROOT / "tests/e2e/fixtures"
WINDOWS = "#{window_index} #{window_name} #{window_layout}"


def geometry(layout):
    body = layout.split(",", 1)[1]
    return re.sub(r"(\d+x\d+,\d+,\d+),\d+", r"\1", body)


def resurrect_server(make_server, tmp_path, mode):
    conf = tmp_path / "resurrect.conf"
    conf.write_text(f"set -g resurrect {mode}\nset -g status-right e2e-ready\n")
    s = make_server()
    s.env["XDG_DATA_HOME"] = str(tmp_path / "data")
    return s, conf


def saved(tmp_path, name):
    return tmp_path / "data/termo/sessions" / f"{name}.json"


def names(server):
    return server.out("list-sessions", "-F", "#{session_name}").split("\n")


def windows(server, session):
    out = []
    for line in server.out("list-windows", "-t", session, "-F", WINDOWS).split("\n"):
        head, layout = line.rsplit(" ", 1)
        out.append(f"{head} {geometry(layout)}")
    return out


def restart(server, conf):
    server.cmd("kill-server")
    expect(lambda: server.cmd("list-sessions", check=False).returncode != 0,
           what="the server to exit")
    server.start(session="boot", conf=conf, lua_init=FIXTURES / "resurrect_init.lua")


def revive(server, name):
    assert server.out("run-lua", f'return termo.session.revive("{name}")') == name
    expect(lambda: name in names(server), what=f"{name} to be revived")


def test_restart_restores_nothing_and_revive_brings_it_back(make_server, tmp_path):
    a, b = tmp_path / "a", tmp_path / "b"
    a.mkdir()
    b.mkdir()
    server, conf = resurrect_server(make_server, tmp_path, "layout")
    server.start("-c", str(a), "-n", "edit", session="work", conf=conf)
    server.cmd("split-window", "-h", "-t", "work", "-c", str(b))
    server.cmd("new-window", "-d", "-t", "work:5", "-n", "logs", "-c", str(b))
    server.cmd("select-pane", "-t", "work:0.0")
    before = windows(server, "work")
    restart(server, conf)
    assert names(server) == ["boot"]
    listed = server.out("run-lua", 'local s = termo.session.list()[1] '
                        'return s.name .. " " .. tostring(s.alive)')
    assert listed == "work false"
    revive(server, "work")
    assert windows(server, "work") == before
    paths = server.out("list-panes", "-s", "-t", "work", "-F", "#{pane_current_path}")
    assert paths.split("\n") == [str(a.resolve()), str(b.resolve()), str(b.resolve())]


def test_attach_revives_a_dead_session(make_server, tmp_path):
    server, conf = resurrect_server(make_server, tmp_path, "layout")
    server.start("-n", "edit", session="work", conf=conf)
    server.cmd("split-window", "-h", "-t", "work")
    restart(server, conf)
    assert names(server) == ["boot"]
    server.attach_pty(target="work")
    expect(lambda: server.out("list-clients", "-F", "#{session_name}") == "work",
           what="the client attached to work")
    assert server.out("list-panes", "-t", "work", "-F", "#{pane_index}").split() == ["0", "1"]


def test_attach_to_an_unknown_session_still_fails(make_server, tmp_path):
    server, conf = resurrect_server(make_server, tmp_path, "layout")
    server.start(session="work", conf=conf)
    r = server.cmd("attach-session", "-t", "nope", check=False)
    assert r.returncode != 0
    assert "can't find session" in r.stderr


def test_a_killed_session_is_kept_and_can_be_revived(make_server, tmp_path):
    server, conf = resurrect_server(make_server, tmp_path, "layout")
    server.start(session="keep", conf=conf, lua_init=FIXTURES / "resurrect_init.lua")
    server.cmd("new-session", "-d", "-s", "notas")
    server.cmd("rename-window", "-t", "notas:0", "scratch")
    expect(lambda: saved(tmp_path, "notas").exists(), what="notas.json to be written")
    server.cmd("kill-session", "-t", "notas")
    assert saved(tmp_path, "notas").exists()
    revive(server, "notas")
    assert server.fmt("#{window_name}", "notas:0") == "scratch"


def test_writes_wait_for_the_delay_and_exit_writes_at_once(make_server, tmp_path):
    server, conf = resurrect_server(make_server, tmp_path, "layout")
    server.start(session="work", conf=conf)
    server.cmd("run-lua", "termo.session.delay = 30000")
    server.cmd("split-window", "-t", "work")
    assert not saved(tmp_path, "work").exists()
    server.cmd("kill-server")
    assert saved(tmp_path, "work").exists()
    mtime = os.stat(saved(tmp_path, "work")).st_mtime_ns
    server.start(session="other", conf=conf)
    revive(server, "work")
    server.cmd("kill-session", "-t", "other")
    server.cmd("run-lua", "termo.session.save_all()")
    expect(lambda: "work" in names(server), what="work alive")
    assert len(server.out("list-panes", "-t", "work", "-F", "x").split()) == 2
    assert os.stat(saved(tmp_path, "work")).st_mtime_ns >= mtime


def test_commands_mode_leaves_the_command_waiting_for_enter(make_server, tmp_path):
    log = tmp_path / "log"
    log.write_text("x\n")
    server, conf = resurrect_server(make_server, tmp_path, "commands")
    server.start(session="work", conf=conf)
    server.cmd("send-keys", "-t", "work", f"tail -f {log}", "Enter")
    expect_fmt(server, "#{pane_current_command}", "tail", target="work")
    server.cmd("run-lua", "termo.session.save_all()")
    expect(lambda: saved(tmp_path, "work").exists()
           and "tail -f" in saved(tmp_path, "work").read_text(),
           what="the command in work.json")
    restart(server, conf)
    revive(server, "work")
    expect(lambda: f"tail -f {log}" in "".join(server.capture("work")),
           what="the command typed at the prompt")
    assert server.fmt("#{pane_current_command}", "work") != "tail"
    server.cmd("send-keys", "-t", "work", "Enter")
    expect_fmt(server, "#{pane_current_command}", "tail", target="work")


def test_screen_mode_restores_the_pane_contents(make_server, tmp_path):
    server, conf = resurrect_server(make_server, tmp_path, "screen")
    server.start(session="work", conf=conf)
    server.cmd("send-keys", "-t", "work", "echo resurrect-marker", "Enter")
    expect(lambda: "resurrect-marker" in "\n".join(server.capture("work")),
           what="the marker on screen")
    server.cmd("run-lua", "termo.session.save_all()")
    screen = tmp_path / "data/termo/sessions/work.0.1.screen"
    expect(lambda: screen.exists() and "resurrect-marker" in screen.read_text(),
           what="the screen file")
    restart(server, conf)
    revive(server, "work")
    expect(lambda: "resurrect-marker" in "\n".join(server.capture("work")),
           what="the marker restored")


def test_off_saves_nothing(make_server, tmp_path):
    server, conf = resurrect_server(make_server, tmp_path, "off")
    server.start(session="work", conf=conf)
    server.cmd("kill-server")
    assert not saved(tmp_path, "work").exists()
