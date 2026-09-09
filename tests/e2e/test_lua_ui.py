import pytest

from termo import ROOT, expect_fmt

FIXTURES = ROOT / "tests/e2e/fixtures"

pytestmark = [pytest.mark.lua, pytest.mark.pty]


@pytest.fixture
def ui(server):
    server.start(conf=FIXTURES / "pty.conf", lua_init=FIXTURES / "lua_ui_init.lua")
    assert server.option("@ready") == "yes"
    return server.attach_pty(marker="lua-status-ok")


def test_key_handler_gets_the_event(server, ui):
    ui.send_keys("C-b", "F5")
    expect_fmt(server, "#{@key}", "F5/prefix/true")


def test_menu_runs_the_chosen_function(server, ui):
    ui.send_keys("C-b", "F6")
    ui.wait_for("Pick one")
    ui.wait_for("Third")
    ui.send_keys("b")
    expect_fmt(server, "#{@picked}", "two/2/b")
    assert server.option("@menu_closed") is None


def test_menu_runs_a_command_item_and_closes(server, ui):
    ui.send_keys("C-b", "F6")
    expect_fmt(server, "#{@menu_open}", "1")
    ui.send_keys("c")
    expect_fmt(server, "#{@picked}", "three")
    ui.send_keys("C-b", "F6")
    expect_fmt(server, "#{@menu_open}", "2")
    ui.send_keys("q")
    expect_fmt(server, "#{@menu_closed}", "yes")


def test_prompt_delivers_the_text(server, ui):
    ui.send_keys("C-b", "F7")
    ui.wait_for("Name:")
    ui.send_text("hello")
    ui.send_keys("Enter")
    expect_fmt(server, "#{@prompt}", "hello/true")


def test_prompt_cancelled_gives_nil(server, ui):
    ui.send_keys("C-b", "F7")
    ui.wait_for("Name:")
    ui.send_keys("Escape")
    expect_fmt(server, "#{@prompt}", "nil/true")


def test_popup_shows_output_and_reports_exit(server, ui):
    ui.send_keys("C-b", "F8")
    ui.wait_for("popup-ok")
    expect_fmt(server, "#{@popup}", "0")


def test_message_reaches_the_status_line(server, ui):
    ui.send_keys("C-b", "F9")
    ui.wait_for("hello from lua")


def test_palette_filters_and_runs(server, ui):
    ui.send_keys("C-b", "F10")
    ui.wait_for(r"\r\n>")
    ui.send_text("mark")
    ui.wait_for("Mark spot")
    ui.send_keys("Enter")
    expect_fmt(server, "#{@palette}", "marked")


def test_api_cmd_from_key_handler_runs_after_it(server, ui):
    ui.send_keys("C-b", "F4")
    expect_fmt(server, "#{@from_cmd}", "yes")
