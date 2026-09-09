from termo import expect, expect_fmt


def test_attach_handshake_and_command_block(server):
    server.start()
    ctl = server.attach_control()
    block = ctl.run("display-message -p hi")
    assert block.ok
    assert block.lines == ["hi"]
    assert block.flags == 1
    assert "no-output" in ctl.flags()


def test_error_blocks_for_unknown_and_unparsable(server):
    server.start()
    ctl = server.attach_control()
    block = ctl.run("no-such")
    assert block.kind == "error"
    assert "unknown command: no-such" in block.text
    block = ctl.run("'")
    assert block.kind == "error"
    assert block.text.startswith("parse error:")


def test_layout_change_window_add_rename_and_pane_change(server):
    server.start()
    ctl = server.attach_control()
    ctl.run("split-window -h")
    note = ctl.expect("%layout-change")
    assert note.args[0] == "@0"
    assert ",80x24,0,0{" in note.args[1]
    ctl.run("select-pane -t %0")
    assert ctl.expect("%window-pane-changed").args == ["@0", "%0"]
    ctl.run("new-window")
    assert ctl.expect("%window-add").args == ["@1"]
    ctl.run("rename-window x")
    ctl.expect("%window-renamed", lambda n: n.args == ["@1", "x"])


def test_sessions_changed_on_create_and_kill(server):
    server.start()
    ctl = server.attach_control()
    ctl.run("new-session -d -s extra")
    ctl.expect("%sessions-changed")
    ctl.run("kill-session -t extra")
    ctl.expect("%sessions-changed")


def test_output_notification_carries_pane_data(server):
    server.start(command="cat")
    ctl = server.attach_control(output=True)
    assert "no-output" not in ctl.flags()
    ctl.run("send-keys -l ping")
    ctl.run("send-keys Tab Enter")
    data = ctl.expect_output("%0", "ping\t", since=0)
    assert "ping\t" in data


def test_client_size_via_refresh_client(server):
    server.start()
    ctl = server.attach_control()
    ctl.run("refresh-client -C 100x50")
    expect_fmt(server, "#{window_width} #{window_height}", "100 50")
    ctl.run("refresh-client -C @0:40x10")
    expect_fmt(server, "#{window_width} #{window_height}", "40 10", target="@0")


def test_subscription_reports_format_changes(server):
    server.start()
    ctl = server.attach_control()
    ctl.run("refresh-client -B 'sw::#{session_windows}'")
    ctl.run("new-window")
    note = ctl.expect("%subscription-changed", lambda n: n.args[0] == "sw" and n.args[-1] == "2",
                      since=0)
    assert note.args[1] == "$0"


def test_pause_and_continue_notifications(server):
    server.start()
    ctl = server.attach_control()
    ctl.run("refresh-client -A '%0:pause'")
    assert ctl.expect("%pause").args == ["%0"]
    ctl.run("refresh-client -A '%0:continue'")
    assert ctl.expect("%continue").args == ["%0"]


def test_prefix_binding_via_send_keys_to_client(server):
    server.start()
    ctl = server.attach_control()
    server.cmd("send-keys", "-K", "-c", ctl.client_name(), "C-b", "c")
    ctl.expect("%window-add", since=0)


def test_empty_line_exits_and_kill_server_exits(server):
    server.start()
    ctl = server.attach_control()
    assert ctl.close() == 0
    assert ctl.notes(kind="%exit")
    ctl2 = server.attach_control()
    server.cmd("kill-server")
    ctl2.expect("%exit")
    assert expect(lambda: ctl2.proc.poll() is not None or None, what="the client to exit")
