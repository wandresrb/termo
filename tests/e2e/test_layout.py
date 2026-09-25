import re

from termo import ROOT, expect_fmt, expect_screen

FIXTURES = ROOT / "tests/e2e/fixtures"

GEOM = "#{pane_left},#{pane_top},#{pane_width},#{pane_height}"
FLOAT = "#{pane_width} #{pane_height} #{pane_left} #{pane_top}"


def geometry(server):
    return server.out("list-panes", "-F", "#{pane_id} " + GEOM).split("\n")


def three_panes(server):
    server.start()
    server.cmd("split-window", "-h")
    server.cmd("split-window", "-v", "-t", "1")


def float_pane(server, *args):
    return server.out("new-pane", "-dPF", "#{pane_id}", *args, "sleep 100")


def test_split_geometry_and_layout_string(server):
    three_panes(server)
    assert geometry(server) == ["%0 0,0,40,24", "%1 41,0,39,12", "%2 41,13,39,11"]
    layout = server.fmt("#{window_layout}")
    assert re.fullmatch(r"[0-9a-f]{4},80x24,0,0\{40x24,0,0,0,39x24,41,0"
                        r"\[39x12,41,0,1,39x11,41,13,2\]\}", layout)


def test_select_layout_presets(server):
    three_panes(server)
    server.cmd("select-layout", "even-horizontal")
    assert geometry(server) == ["%0 0,0,26,24", "%1 27,0,26,24", "%2 54,0,26,24"]
    server.cmd("select-layout", "tiled")
    assert geometry(server) == ["%0 0,0,39,11", "%1 40,0,40,11", "%2 0,12,80,12"]
    server.cmd("set", "-w", "main-pane-width", "50")
    server.cmd("select-layout", "main-vertical")
    assert geometry(server) == ["%0 0,0,50,24", "%1 51,0,29,12", "%2 51,13,29,11"]


def test_resize_pane_absolute_relative_zoom(server):
    server.start()
    server.cmd("split-window", "-v")
    assert geometry(server) == ["%0 0,0,80,12", "%1 0,13,80,11"]
    server.cmd("resize-pane", "-t", "0", "-y", "5")
    assert geometry(server) == ["%0 0,0,80,5", "%1 0,6,80,18"]
    server.cmd("resize-pane", "-t", "0", "-D", "3")
    assert geometry(server) == ["%0 0,0,80,8", "%1 0,9,80,15"]
    server.cmd("resize-pane", "-Z")
    assert server.fmt("#{window_zoomed_flag} #{pane_id} #{pane_width}x#{pane_height}") == "1 %1 80x24"
    server.cmd("resize-pane", "-Z")
    assert server.fmt("#{window_zoomed_flag}") == "0"
    assert geometry(server) == ["%0 0,0,80,8", "%1 0,9,80,15"]


def test_floating_pane_geometry_with_border(server):
    server.start()
    fid = float_pane(server, "-x", "20", "-y", "6", "-X", "8", "-Y", "3")
    assert server.fmt("#{pane_floating_flag}", fid) == "1"
    assert server.fmt(FLOAT, fid) == "18 4 9 4"
    server.cmd("resize-pane", "-t", fid, "-x", "30")
    assert server.fmt(FLOAT, fid) == "28 4 9 4"
    server.cmd("resize-pane", "-t", fid, "-x", "75%")
    assert server.fmt(FLOAT, fid) == "58 4 9 4"
    server.cmd("resize-pane", "-t", fid, "-y", "50%")
    assert server.fmt(FLOAT, fid) == "58 10 9 4"
    assert server.fmt(GEOM, "%0") == "0,0,80,24"


def test_floating_pane_geometry_without_border(server):
    server.start()
    fid = float_pane(server, "-B", "none", "-x", "20", "-y", "6", "-X", "8", "-Y", "3")
    assert server.fmt(FLOAT, fid) == "20 6 8 3"
    server.cmd("resize-pane", "-t", fid, "-x", "30")
    assert server.fmt(FLOAT, fid) == "30 6 8 3"
    server.cmd("move-pane", "-t", fid, "-X", "5", "-Y", "1")
    assert server.fmt(FLOAT, fid) == "30 6 5 1"


def test_floating_pane_rejects_zero_sizes(server):
    server.start()
    r = server.cmd("new-pane", "-d", "-x", "0", "-y", "6", "-X", "8", "-Y", "3", "sleep 100",
                   check=False)
    assert (r.returncode, r.stderr) == (1, "invalid width\n")
    r = server.cmd("new-pane", "-d", "-x", "20", "-y", "0", "-X", "8", "-Y", "3", "sleep 100",
                   check=False)
    assert (r.returncode, r.stderr) == (1, "invalid height\n")
    assert server.pane_ids() == ["%0"]
    fid = float_pane(server, "-x", "20", "-y", "6", "-X", "8", "-Y", "3")
    for axis in ("-x", "-y"):
        r = server.cmd("resize-pane", "-t", fid, axis, "0", check=False)
        assert (r.returncode, r.stderr) == (1, "size is too big or too small\n")
    assert server.fmt(FLOAT, fid) == "18 4 9 4"


def test_tiled_resize_skips_floating_cells(server):
    server.start()
    fid = float_pane(server, "-x", "50%", "-y", "50%", "-X", "50%", "-Y", "50%")
    assert server.fmt(FLOAT, fid) == "38 10 41 13"
    server.cmd("split-window", "-v")
    assert geometry(server) == ["%0 0,0,80,12", "%2 0,13,80,11", "%1 41,13,38,10"]
    server.cmd("resize-pane", "-t", "%2", "-U", "5")
    assert geometry(server) == ["%0 0,0,80,7", "%2 0,8,80,16", "%1 41,13,38,10"]


def test_float_options_size_and_centre_a_new_pane(server):
    server.start()
    server.cmd("set", "-w", "float-width", "60")
    server.cmd("set", "-w", "float-height", "50%")
    server.cmd("set", "-w", "float-position", "centre")
    fid = float_pane(server)
    assert server.fmt(FLOAT, fid) == "58 10 11 7"
    fid = float_pane(server, "-x", "20", "-X", "0")
    assert server.fmt(FLOAT, fid) == "18 10 1 7"
    r = server.cmd("set", "-w", "float-position", "middle", check=False)
    assert r.returncode != 0


def test_default_resize_keys_route_to_the_float(server):
    server.start()
    ctl = server.attach_control()
    fid = float_pane(server, "-x", "20", "-y", "6", "-X", "8", "-Y", "3")
    server.cmd("select-pane", "-t", fid)
    server.cmd("send-keys", "-K", "-c", ctl.client_name(), "C-b", "C-Up")
    assert server.fmt(FLOAT, fid) == "18 3 9 4"
    server.cmd("send-keys", "-K", "-c", ctl.client_name(), "C-b", "M-Left")
    assert server.fmt(FLOAT, fid) == "13 3 9 4"


STACK = "#{pane_id} #{pane_collapsed_flag} #{pane_top} #{pane_height} #{pane_stack_index}/#{pane_stack_size}"


def stack_geometry(server):
    return server.out("list-panes", "-F", STACK).split("\n")


def test_split_into_stack_geometry_and_layout_string(server):
    server.start()
    server.cmd("stack-pane")
    assert stack_geometry(server) == ["%0 1 0 24 1/2", "%1 0 1 23 2/2"]
    assert server.fmt("#{pane_id}") == "%1"
    assert "(" in server.fmt("#{window_layout}")
    assert server.fmt("#{window_stacks}") == "1"
    assert server.fmt("#{pane_stacked_flag}", "%0") == "1"
    server.cmd("stack-pane")
    assert stack_geometry(server) == ["%0 1 0 24 1/3", "%1 1 1 23 2/3", "%2 0 2 22 3/3"]


def test_select_pane_expands_a_collapsed_pane_and_cycles(server):
    server.start()
    server.cmd("stack-pane")
    server.cmd("select-pane", "-t", "%0")
    assert stack_geometry(server) == ["%0 0 0 23 1/2", "%1 1 23 23 2/2"]
    server.cmd("stack-pane", "-n")
    assert server.fmt("#{pane_id}") == "%1"
    assert stack_geometry(server) == ["%0 1 0 23 1/2", "%1 0 1 23 2/2"]


def test_only_the_expanded_pane_grows_on_window_resize(server):
    server.start()
    server.cmd("stack-pane")
    server.cmd("resize-window", "-y", "30")
    assert stack_geometry(server) == ["%0 1 0 24 1/2", "%1 0 1 29 2/2"]


def test_zoom_and_unzoom_restore_the_stack(server):
    server.start()
    server.cmd("stack-pane")
    server.cmd("resize-pane", "-Z")
    assert server.fmt("#{window_zoomed_flag}") == "1"
    server.cmd("resize-pane", "-Z")
    assert stack_geometry(server) == ["%0 1 0 24 1/2", "%1 0 1 23 2/2"]
    server.cmd("resize-pane", "-Z", "-t", "%0")
    assert server.fmt("#{pane_id}") == "%0"
    server.cmd("resize-pane", "-Z")
    assert stack_geometry(server) == ["%0 0 0 23 1/2", "%1 1 23 23 2/2"]


def test_float_over_a_stack_is_unaffected(server):
    server.start()
    server.cmd("stack-pane")
    fid = float_pane(server, "-x", "20", "-y", "6", "-X", "8", "-Y", "3")
    assert server.fmt(FLOAT, fid) == "18 4 9 4"
    assert stack_geometry(server)[:2] == ["%0 1 0 24 1/2", "%1 0 1 23 2/2"]


def test_collapsed_row_shows_the_pane_title(make_server):
    inner = make_server()
    conf = inner.tmpdir / "stack.conf"
    conf.write_text("set -g status off\n")
    inner.start("-n", "win", conf=conf, command="sleep 100")
    inner.cmd("stack-pane", "sleep 100")
    inner.cmd("select-pane", "-T", "collapsed-title", "-t", "%0")
    outer = make_server()
    pane = outer.nest(inner)
    expect_screen(outer, pane, [r"{MATCH:.*collapsed-title.*}"] + [r"{MATCH:.*}"] * 23)


def test_click_on_a_collapsed_row_expands_it(server):
    server.start(conf=FIXTURES / "pty.conf")
    server.cmd("set", "-g", "mouse", "on")
    pty = server.attach_pty()
    expect_fmt(server, "#{window_height}", "23")
    server.cmd("stack-pane")
    assert stack_geometry(server) == ["%0 1 0 23 1/2", "%1 0 1 22 2/2"]
    pty.send_text("\x1b[<0;5;1M\x1b[<0;5;1m")
    expect_fmt(server, "#{pane_collapsed_flag}", "0", target="%0")
    assert stack_geometry(server) == ["%0 0 0 22 1/2", "%1 1 22 22 2/2"]
    pty.close()


def test_stack_pane_moves_an_existing_pane_into_a_stack(server):
    server.start()
    server.cmd("split-window", "-h", "-d")
    assert server.out("list-panes", "-F", "#{pane_id}").split() == ["%0", "%1"]
    server.cmd("stack-pane", "-s", "%1", "-t", "%0")
    assert stack_geometry(server) == ["%0 1 0 24 1/2", "%1 0 1 23 2/2"]
    assert "(" in server.fmt("#{window_layout}")


def test_new_pane_modal_flag_is_back(server):
    server.start()
    fid = float_pane(server, "-O")
    assert server.fmt("#{pane_modal_flag}", fid) == "1"
