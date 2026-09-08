import re

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
