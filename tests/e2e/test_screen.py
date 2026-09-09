from termo import ROOT, expect_fmt, expect_screen

TWO_LINES = "sh -c 'printf \"A line of words\\nsecond line here\\n\"; exec sleep 100'"


def test_pane_output_reaches_capture_pane(server):
    server.start(command="sh -c 'printf \"hello\\nworld\\n\"; exec sleep 100'")
    expect_screen(server, None, ["hello", "world"])


def test_send_keys_typed_into_cat_are_echoed(server):
    server.start(command="cat")
    server.cmd("send-keys", "-l", "abc")
    server.cmd("send-keys", "Enter")
    expect_screen(server, None, ["abc", "abc"])


def test_scrollback_is_captured_with_negative_start(server):
    server.start(command="sh -c 'seq -w 1 30; exec sleep 100'")
    expect_fmt(server, "#{history_size}", "7")
    assert server.capture(start=-7, end=-1) == ["01", "02", "03", "04", "05", "06", "07"]
    assert server.capture()[:2] == ["08", "09"]


def test_capture_pane_e_keeps_sgr(server):
    server.start(command="sh -c 'printf \"\\033[1mbold\\033[0m plain\\n\"; exec sleep 100'")
    expect_screen(server, None, ["bold plain"])
    assert server.capture(escapes=True)[0] == "\x1b[1mbold\x1b[0m plain"


def select(server, before, after):
    server.cmd("copy-mode")
    server.cmd("send-keys", "-X", "history-top")
    server.cmd("send-keys", "-X", "start-of-line")
    for motion in before:
        server.cmd("send-keys", "-X", motion)
    server.cmd("send-keys", "-X", "begin-selection")
    for motion in after:
        server.cmd("send-keys", "-X", motion)
    server.cmd("send-keys", "-X", "copy-selection-and-cancel")
    expect_fmt(server, "#{pane_in_mode}", "0")
    return server.out("show-buffer")


def test_copy_mode_vi_word_motions_copy_selection(server):
    server.start(command=TWO_LINES)
    server.cmd("set", "-g", "mode-keys", "vi")
    expect_screen(server, None, ["A line of words", "second line here"])
    assert select(server, [], ["next-word-end"]) == "A line"
    assert select(server, ["cursor-down"], ["next-space-end"]) == "second"
    assert select(server, ["end-of-line"], ["previous-word"]) == "words"


def test_copy_mode_vi_keys_through_client_table(server):
    server.start(conf=ROOT / "etc/termo.conf", command=TWO_LINES)
    expect_screen(server, None, ["A line of words", "second line here"])
    server.cmd("copy-mode")
    ctl = server.attach_control()
    server.cmd("send-keys", "-K", "-c", ctl.client_name(), "g", "0", "v", "e", "y")
    expect_fmt(server, "#{pane_in_mode}", "0")
    assert server.out("show-buffer") == "A line"


def test_status_line_renders_session_and_window(make_server, tmp_path):
    conf = tmp_path / "status.conf"
    conf.write_text("set -g status-left '[#S] '\n"
                    "set -g status-right ''\n"
                    "set -g status-interval 0\n")
    inner = make_server()
    inner.start("-n", "win", conf=conf, command="sleep 100")
    outer = make_server()
    pane = outer.nest(inner)
    status = r"{MATCH:\[main\] 0:win\*.*}"
    expect_screen(outer, pane, [""] * 23 + [status])
    inner.cmd("set", "-g", "status-position", "top")
    expect_screen(outer, pane, [status] + [""] * 23)
