from termo import expect, expect_fmt, expect_screen


def test_session_window_pane_lifecycle(server):
    server.start(session="life")
    assert server.out("list-sessions", "-F", "#{session_name}") == "life"
    server.cmd("new-window", "-t", "life:")
    assert server.out("list-windows", "-t", "life", "-F", "#{window_index}").split() == ["0", "1"]
    server.cmd("split-window", "-t", "life:1")
    assert len(server.pane_ids("life:1")) == 2
    server.cmd("kill-window", "-t", "life:1")
    assert server.out("list-windows", "-t", "life", "-F", "#{window_index}") == "0"
    server.cmd("kill-session", "-t", "life")
    assert server.cmd("list-sessions", check=False).returncode != 0


def test_targets_resolve_by_id_name_and_prefix(server):
    server.start(session="alpha")
    server.cmd("new-session", "-d", "-s", "beta")
    server.cmd("rename-window", "-t", "alpha:0", "editor")
    server.cmd("new-window", "-t", "alpha:", "-n", "editing")
    server.cmd("new-window", "-t", "alpha:", "-n", "shell")
    assert server.fmt("#{session_name}", "al:") == "alpha"
    assert server.fmt("#{session_name}", "=alpha:") == "alpha"
    sid = server.fmt("#{session_id}", "alpha:")
    assert server.fmt("#{session_name}", f"{sid}:") == "alpha"
    assert server.fmt("#{window_name}", "alpha:shell") == "shell"
    assert server.fmt("#{window_name}", "alpha:editi") == "editing"
    wid = server.fmt("#{window_id}", "alpha:shell")
    assert server.fmt("#{window_name}", f"alpha:{wid}") == "shell"
    server.cmd("new-session", "-d", "-s", "grp1")
    server.cmd("new-session", "-d", "-s", "grp2")
    r = server.cmd("has-session", "-t", "grp:", check=False)
    assert r.returncode != 0
    assert "can't find session: grp" in r.stderr


def test_renumber_windows_and_base_index(server):
    server.start()
    server.cmd("set", "-g", "base-index", "1")
    server.cmd("set", "-g", "renumber-windows", "on")
    server.cmd("new-window")
    server.cmd("new-window")
    assert server.out("list-windows", "-F", "#{window_index}").split() == ["0", "1", "2"]
    server.cmd("kill-window", "-t", ":1")
    assert server.out("list-windows", "-F", "#{window_index}").split() == ["1", "2"]


def test_option_scopes_and_inheritance(server):
    server.start()
    server.cmd("set", "-g", "@x", "g")
    server.cmd("set", "-t", "main", "@x", "s")
    assert server.option("@x", "v", "main") == "s"
    server.cmd("set", "-u", "-t", "main", "@x")
    assert server.option("@x", "v", "main") is None
    assert server.option("@x", "Av", "main") == "g"
    server.cmd("set", "-p", "@p", "1")
    assert server.option("@p", "pv") == "1"


def test_paste_buffers_stack_and_paste(server):
    server.start(command="cat")
    server.cmd("set-buffer", "one")
    server.cmd("set-buffer", "two")
    assert server.out("list-buffers", "-F", "#{buffer_name}=#{buffer_sample}").split() == \
        ["buffer1=two", "buffer0=one"]
    assert server.out("show-buffer") == "two"
    server.cmd("paste-buffer")
    expect_screen(server, None, ["two"])


def test_buffer_limit_keeps_named_buffers(server):
    server.start()
    server.cmd("set-buffer", "-b", "keep", "k")
    server.cmd("set", "-g", "buffer-limit", "2")
    for text in "abc":
        server.cmd("set-buffer", text)
    assert server.out("list-buffers", "-F", "#{buffer_name}").split() == \
        ["buffer2", "buffer1", "keep"]


def test_hook_fires_with_hook_formats(server):
    server.start()
    server.cmd("set-hook", "-g", "session-created",
               'set -gF @created "#{hook}:#{hook_session_name}"')
    server.cmd("new-session", "-d", "-s", "two")
    expect_fmt(server, "#{@created}", "session-created:two")
    assert "session-created[0]" in server.out("show-hooks", "-g", "session-created")
    server.cmd("set-hook", "-gu", "session-created")
    server.cmd("new-session", "-d", "-s", "three")
    server.cmd("display-message", "-p", "fence")
    assert server.fmt("#{@created}") == "session-created:two"


def test_wait_for_signal_wakes_waiter(server):
    server.start()
    waiter = server.popen("wait-for", "chan")
    expect(lambda: server.out("wait-for", "-l", "chan") != "", what="a waiter on chan")
    server.cmd("wait-for", "-S", "chan")
    assert expect(lambda: waiter.poll() is not None or None, what="the waiter to exit")
    assert waiter.returncode == 0


def test_session_environment_set_show_hidden_unset(server):
    server.start()
    server.cmd("set-environment", "-t", "main", "FOO", "bar")
    assert server.out("show-environment", "-t", "main", "FOO") == "FOO=bar"
    server.cmd("set-environment", "-g", "-h", "HID", "x")
    assert server.cmd("show-environment", "-g", "HID", check=False).stdout == ""
    assert server.out("show-environment", "-gh", "HID") == "HID=x"
    server.cmd("set-environment", "-g", "-r", "HID")
    assert server.out("show-environment", "-gh", "HID") == "-HID"


def test_new_session_environment_reaches_pane(server):
    server.start("-e", "FOO=bar", command="sh -c 'echo FOO=$FOO; exec sleep 100'")
    expect_screen(server, None, ["FOO=bar"])
