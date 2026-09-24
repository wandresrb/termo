import shutil
import subprocess

import pytest

from termo import ROOT, expect_fmt

FIXTURES = ROOT / "tests/e2e/fixtures"


def test_run_lua_returns_value_and_json(server):
    server.start()
    assert server.out("run-lua", "return 1 + 1") == "2"
    assert server.out("run-lua", "-j", "return {a = 1}") == '{"a":1}'


def test_run_lua_error_is_reported(server):
    server.start()
    r = server.cmd("run-lua", "error('boom')", check=False)
    assert r.returncode != 0
    assert "boom" in r.stderr


def test_runtime_modules_resolve_via_TERMO_RUNTIME(server):
    server.start()
    assert server.out("run-lua", "return type(termo.keymap.set)") == "function"


def user_home(server, tmp_path):
    home = tmp_path / "home"
    (home / ".config/termo").mkdir(parents=True)
    server.env["HOME"] = str(home)
    server.env.pop("XDG_CONFIG_HOME", None)
    return home


def test_defaults_load_without_f(server, tmp_path):
    user_home(server, tmp_path)
    server.start(conf=None)
    assert server.option("mouse") == "on"
    assert server.option("resurrect") == "on"


def test_init_lua_wins_over_a_conf_and_reports_the_conflict(server, tmp_path):
    home = user_home(server, tmp_path)
    (home / ".config/termo/termo.conf").write_text("set -g @conf loaded\n")
    (home / ".config/termo/init.lua").write_text('termo.api.set_option("@lua", "loaded")\n')
    server.start(conf=None)
    assert server.option("@lua") == "loaded"
    assert server.option("@conf") is None
    ctl = server.attach_control()
    note = ctl.expect("%config-error", since=0)
    assert "E5422" in " ".join(note.args)


def test_only_the_first_conf_is_loaded(server, tmp_path):
    home = user_home(server, tmp_path)
    (home / ".config/termo/termo.conf").write_text("set -g @first yes\n")
    (home / ".tmux.conf").write_text("set -g @second yes\n")
    server.start(conf=None)
    assert server.option("@first") == "yes"
    assert server.option("@second") is None


def test_pack_loads_a_native_lib_through_ffi(server, tmp_path):
    cc = shutil.which("cc")
    if cc is None:
        pytest.skip("no C compiler")
    plugin = tmp_path / "ffiplug"
    (plugin / "lua").mkdir(parents=True)
    subprocess.run([cc, "-shared", "-fPIC", "-o", str(plugin / "libffiplug.so"),
                    str(FIXTURES / "plugin_lib.c")], check=True)
    (plugin / "termo.json").write_text('{"name": "ffiplug", "lib": "libffiplug.so"}\n')
    (plugin / "lua/ffiplug.lua").write_text(
        'local plugin = ...\n'
        'local ffi = require("ffi")\n'
        'ffi.cdef[[int termo_plugin_add(int, int);]]\n'
        'termo.api.set_option("@ffi_sum", tostring(plugin.lib.termo_plugin_add(2, 3)))\n'
        'return {}\n')
    server.start()
    server.cmd("run-lua", f'assert(termo.pack.load("{plugin}"))')
    assert server.option("@ffi_sum") == "5"


def test_layout_swap_key_cycles_saved_layouts(server, tmp_path):
    server.start()
    ctl = server.attach_control()
    server.cmd("split-window", "-d", "-h")
    wide = server.fmt("#{window_layout}")
    server.cmd("run-lua", f'termo.layout.dir = "{tmp_path}/layouts"')
    server.cmd("run-lua", 'termo.layout.save("1-wide")')
    server.cmd("select-layout", "even-vertical")
    tall = server.fmt("#{window_layout}")
    server.cmd("run-lua", 'termo.layout.save("2-tall")')
    server.cmd("run-lua", 'termo.keymap.set("F9", function() termo.layout.swap() end)')
    server.cmd("send-keys", "-K", "-c", ctl.client_name(), "C-b", "F9")
    expect_fmt(server, "#{window_layout}", wide)
    server.cmd("send-keys", "-K", "-c", ctl.client_name(), "C-b", "F9")
    expect_fmt(server, "#{window_layout}", tall)
