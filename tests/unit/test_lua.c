#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "harness.h"
#include "test.h"

static void
start(void)
{
	setenv("TERMO_RUNTIME", TERMO_SOURCE_ROOT "/runtime", 1);
	termo_lua_init();
}

static char *
eval(const char *code)
{
	char	*result = nullptr;

	termo_lua_eval(code, nullptr, &result, false);
	return (result);
}

static void
expect(const char *file, int line, const char *code, const char *want)
{
	char	*got = eval(code);

	if (got == nullptr || strcmp(got, want) != 0) {
		test_fail(file, line, "%s: got %s%s%s, want \"%s\"", code,
		    got ? "\"" : "", got ? got : "nil", got ? "\"" : "", want);
	}
	free(got);
}
#define EXPECT(code, want) expect(__FILE__, __LINE__, code, want)

TEST(lua, init_and_free_are_idempotent)
{
	start();
	REQUIRE_NONNULL(termo_lua_state());
	start();
	termo_lua_free();
	CHECK_NULL(termo_lua_state());
	termo_lua_free();
	start();
	CHECK_NONNULL(termo_lua_state());
	termo_lua_free();
}

TEST(lua, eval_returns_the_chunk_value)
{
	char	*got;

	start();
	EXPECT("return 1 + 1", "2");
	EXPECT("return 'a' .. 'b'", "ab");
	EXPECT("return true", "true");
	got = eval("local x = 1");
	CHECK_NULL(got);
	CHECK_NULL(termo_lua_item());
	termo_lua_free();
}

TEST(lua, errors_are_reported_not_fatal)
{
	u_int	before = cfg_ncauses;
	char	*got;

	start();
	CHECK_EQ(termo_lua_eval("return (", nullptr, &got, false), -1);
	CHECK_NULL(got);
	CHECK_EQ(cfg_ncauses, before + 1);
	CHECK_EQ(termo_lua_eval("error('boom')", nullptr, &got, false), -1);
	CHECK_EQ(cfg_ncauses, before + 2);
	CHECK_EQ(termo_lua_eval("nope()", nullptr, nullptr, false), -1);
	CHECK_EQ(cfg_ncauses, before + 3);
	EXPECT("return 'still alive'", "still alive");
	termo_lua_free();
}

TEST(lua, budget_cuts_an_endless_loop)
{
	lua_State	*L;
	uint64_t	 t0;
	u_int		 before = cfg_ncauses;

	start();
	L = termo_lua_state();
	REQUIRE_EQ(luaL_loadstring(L, "while true do end"), 0);
	t0 = get_timer();
	CHECK_EQ(termo_lua_call(L, 0, 0, 100, nullptr), -1);
	CHECK(get_timer() - t0 < 2000);
	CHECK_EQ(cfg_ncauses, before + 1);
	CHECK_EQ(lua_gettop(L), 0);
	EXPECT("return 'after'", "after");
	termo_lua_free();
}

TEST(lua, budget_cuts_a_loop_calling_the_api)
{
	lua_State	*L;
	uint64_t	 t0;
	u_int		 before = cfg_ncauses;

	start();
	L = termo_lua_state();
	REQUIRE_EQ(luaL_loadstring(L, "while true do termo.api.version() end"),
	    0);
	t0 = get_timer();
	CHECK_EQ(termo_lua_call(L, 0, 0, 100, nullptr), -1);
	CHECK(get_timer() - t0 < 2000);
	CHECK_EQ(cfg_ncauses, before + 1);
	termo_lua_free();
}

TEST(lua, budget_has_no_off_switch)
{
	start();
	EXPECT("return tostring(jit.status())", "false");
	EXPECT("return tostring(jit.on)", "nil");
	EXPECT("return tostring(debug.sethook)", "nil");
	EXPECT("return tostring(os.execute)", "nil");
	EXPECT("return tostring(io.popen)", "nil");
	EXPECT("return tostring(os.exit)", "nil");
	EXPECT("return tostring(require('os').exit)", "nil");
	termo_lua_free();
}

TEST(lua, api_version_and_list)
{
	start();
	EXPECT("return termo.api.version()", getversion());
	EXPECT("return #termo.api.list()", "24");
	EXPECT("return termo.api.list()[1].name", "version");
	EXPECT("return type(termo.api.list()[3].signature)", "string");
	EXPECT("return termo.version()", getversion());
	termo_lua_free();
}

TEST(lua, api_eval_expands_formats)
{
	start();
	EXPECT("return termo.api.eval('#{version}')", getversion());
	EXPECT("return termo.api.eval('#{e|+|:2,3}')", "5");
	EXPECT("return termo.api.eval('#{?version,yes,no}')", "yes");
	EXPECT("return select(2, pcall(termo.api.eval, '#{x}', '%999'))",
	    "no such target: %999");
	EXPECT("return select(2, pcall(termo.api.eval))",
	    "bad argument #1 to '?' (string expected, got no value)");
	termo_lua_free();
}

TEST(lua, api_options_read_and_write)
{
	start();
	EXPECT("return termo.api.get_option('history-limit')", "2000");
	EXPECT("return termo.api.get_option('renumber-windows')", "false");
	EXPECT("return termo.api.get_option('status')", "on");
	EXPECT("return termo.api.get_option('mode-keys')", "emacs");
	EXPECT("return type(termo.api.get_option('terminal-features'))", "table");
	EXPECT("return termo.api.get_option('default-terminal')", "screen");

	EXPECT("termo.api.set_option('history-limit', 12345) return true", "true");
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 12345);
	EXPECT("termo.api.set_option('renumber-windows', true) return true", "true");
	CHECK_EQ(options_get_number(global_s_options, "renumber-windows"), 1);
	EXPECT("termo.api.set_option('mode-keys', 'vi') return true", "true");
	CHECK_EQ(options_get_number(global_w_options, "mode-keys"), MODEKEY_VI);

	EXPECT("return select(2, pcall(termo.api.set_option, 'history-limit', 'abc'))",
	    "value is invalid: abc");
	EXPECT("return select(2, pcall(termo.api.get_option, 'no-such-option'))",
	    "no such option: no-such-option");
	termo_lua_free();
}

TEST(lua, print_outside_a_command_goes_to_the_log)
{
	start();
	EXPECT("print('x', 1, nil) return 'ok'", "ok");
	termo_lua_free();
}

TEST(lua, cmd_runs_now_outside_a_command)
{
	start();
	EXPECT("return tostring(termo.api.cmd('set -g history-limit 4242'))",
	    "nil");
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 4242);
	EXPECT("return (termo.api.cmd('display -p hi'))", "hi");
	EXPECT("return select(2, termo.api.cmd('set -g no-such-option 1'))",
	    "invalid option: no-such-option");
	EXPECT("return select(2, pcall(termo.api.cmd, 'bogus-command'))",
	    "unknown command: bogus-command");
	termo_lua_free();
}

TEST(lua, cmd_async_calls_back_with_output)
{
	start();
	EXPECT("termo.api.cmd_async('display -p later', function(out, err) "
	    "termo.api.set_option('@async', out .. '/' .. tostring(err)) end) "
	    "return 'queued'", "queued");
	EXPECT("return tostring(termo.api.get_option('@async'))", "nil");
	while (cmdq_next(nullptr) != 0)
		;
	EXPECT("return termo.api.get_option('@async')", "later/nil");
	termo_lua_free();
}

TEST(lua, events_round_trip_and_hold_commands)
{
	start();
	EXPECT("local got; termo.api.on('@t', function(p) got = p.event .. p.a "
	    "termo.api.cmd('set -g @from_sink 1') end) termo.api.emit('@t', "
	    "{a = 'x'}) return got .. '/' .. tostring(termo.api.get_option("
	    "'@from_sink'))", "@tx/nil");
	while (cmdq_next(nullptr) != 0)
		;
	EXPECT("return termo.api.get_option('@from_sink')", "1");
	termo_lua_free();
}

TEST(lua, keymap_binds_run_lua)
{
	struct key_table	*table;
	struct key_binding	*bd;
	char			*s;

	start();
	EXPECT("termo.api.keymap_set('prefix', 'F12', function(ev) "
	    "termo.api.set_option('@key', ev.table) end) return 'ok'", "ok");
	REQUIRE_NONNULL(table = key_bindings_get_table("prefix", 0));
	REQUIRE_NONNULL(bd = key_bindings_get(table, KEYC_F12));
	s = cmd_list_print(bd->cmdlist, 0);
	CHECK_EQ(strncmp(s, "run-lua -r ", 11), 0);
	free(s);
	EXPECT("termo.api.keymap_del('prefix', 'F12') return 'ok'", "ok");
	/* The table went away with its last binding. */
	CHECK_NULL(key_bindings_get_table("prefix", 0));
	termo_lua_free();
}

TEST(lua, format_variables_are_lazy)
{
	char	*s;

	start();
	EXPECT("termo.api.format_add('lua_t', function(n) return n .. '!' end) "
	    "return 'ok'", "ok");
	s = format_single(nullptr, "#{lua_t}", nullptr, nullptr, nullptr, nullptr);
	CHECK_EQ(s, "lua_t!");
	free(s);
	EXPECT("local n = 0 termo.api.format_add('lua_n', function() n = n + 1 "
	    "return n end) termo.api.eval('#{version}') return n", "0");
	EXPECT("return select(2, pcall(termo.api.format_add, 'a-b', print))",
	    "bad variable name: a-b");
	termo_lua_free();
	s = format_single(nullptr, "[#{lua_t}]", nullptr, nullptr, nullptr,
	    nullptr);
	CHECK_EQ(s, "[]");
	free(s);
}

TEST(lua, timers_fire_from_the_loop)
{
	uint64_t	t0;

	start();
	EXPECT("local t = termo.api.timer(1, function() termo.api.set_option("
	    "'@ticks', tostring((tonumber(termo.api.get_option('@ticks')) or 0) "
	    "+ 1)) end) termo.api.defer(20, function() t:stop() end) "
	    "return 'armed'", "armed");
	t0 = get_timer();
	while (get_timer() - t0 < 500) {
		event_base_loop(libevent, EVLOOP_NONBLOCK);
		if (options_get_only(global_s_options, "@ticks") != nullptr &&
		    get_timer() - t0 > 40)
			break;
	}
	EXPECT("return tonumber(termo.api.get_option('@ticks')) >= 3", "true");
	termo_lua_free();
}

/*
 * A Lua error inside a callback item with no client, after the config has
 * finished, goes through cmdq_error with a null command: it must land in
 * the message log, not dereference the command.
 */
TEST(lua, errors_in_callback_items_reach_the_message_log)
{
	struct message_entry	*msg;

	start();
	TAILQ_INIT(&message_log);
	cfg_finished = 1;
	EXPECT("termo.api.cmd_async('display -p x', function() error('boom') end) "
	    "return 'queued'", "queued");
	while (cmdq_next(nullptr) != 0)
		;
	cfg_finished = 0;
	msg = TAILQ_LAST(&message_log, message_list);
	REQUIRE_NONNULL(msg);
	CHECK(strstr(msg->msg, "boom") != nullptr);
	termo_lua_free();
}
