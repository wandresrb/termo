#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
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

	termo_lua_eval(code, nullptr, &result);
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
	CHECK_EQ(termo_lua_eval("return (", nullptr, &got), -1);
	CHECK_NULL(got);
	CHECK_EQ(cfg_ncauses, before + 1);
	CHECK_EQ(termo_lua_eval("error('boom')", nullptr, &got), -1);
	CHECK_EQ(cfg_ncauses, before + 2);
	CHECK_EQ(termo_lua_eval("nope()", nullptr, nullptr), -1);
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
	EXPECT("return #termo.api.list()", "5");
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
