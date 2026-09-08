import pytest

pytestmark = pytest.mark.lua


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


def test_init_lua_loads_after_termo_conf(server, tmp_path):
    home = tmp_path / "home"
    conf = home / ".config/termo"
    conf.mkdir(parents=True)
    (conf / "termo.conf").write_text("set -g @order conf\n")
    (conf / "init.lua").write_text(
        'termo.api.set_option("@order", termo.api.get_option("@order") .. "+lua")\n')
    server.env["HOME"] = str(home)
    server.start(conf=None)
    assert server.option("@order") == "conf+lua"
