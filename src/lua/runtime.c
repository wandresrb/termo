/*
 * The Lua runtime: one LuaJIT state inside the server, entered only through
 * termo_lua_call, which bounds wall time and routes errors to wherever the
 * caller's output goes.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <luajit.h>
#include <lualib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

constexpr u_int TERMO_LUA_HOOK_COUNT = 10000;

static lua_State	*state;
static struct cmdq_item	*current_item;
static uint64_t		 deadline;
static u_int		 depth;

lua_State *
termo_lua_state(void)
{
	return (state);
}

struct cmdq_item *
termo_lua_item(void)
{
	return (current_item);
}

static void
budget_hook(lua_State *L, [[maybe_unused]] lua_Debug *ar)
{
	if (get_timer() < deadline)
		return;
	lua_sethook(L, nullptr, 0, 0);
	luaL_error(L, "budget of %u ms exceeded", TERMO_LUA_BUDGET_MS);
}

static int
traceback(lua_State *L)
{
	const char	*msg = lua_tostring(L, 1);

	if (msg == nullptr)
		msg = "(error object is not a string)";
	luaL_traceback(L, L, msg, 1);
	return (1);
}

/* print(): to the running command if there is one, otherwise the log. */
static int
lua_print(lua_State *L)
{
	int		 n = lua_gettop(L), i;
	luaL_Buffer	 b;
	const char	*s;

	luaL_buffinit(L, &b);
	for (i = 1; i <= n; i++) {
		lua_getglobal(L, "tostring");
		lua_pushvalue(L, i);
		lua_call(L, 1, 1);
		s = lua_tostring(L, -1);
		if (s == nullptr)
			return (luaL_error(L, "tostring must return a string"));
		if (i > 1)
			luaL_addchar(&b, '\t');
		luaL_addstring(&b, s);
		lua_pop(L, 1);
	}
	luaL_pushresult(&b);
	s = lua_tostring(L, -1);
	if (current_item != nullptr)
		cmdq_print(current_item, "%s", s);
	else
		log_debug("lua: %s", s);
	return (0);
}

static void
report(struct cmdq_item *item, const char *msg)
{
	if (!cfg_finished)
		cfg_add_cause("%s", msg);
	else if (item != nullptr)
		cmdq_error(item, "%s", msg);
	else
		server_add_message("lua: %s", msg);
}

int
termo_lua_call(lua_State *L, int nargs, int nresults, u_int budget_ms,
    struct cmdq_item *item)
{
	struct cmdq_item	*saved_item = current_item;
	uint64_t		 saved_deadline = deadline;
	int			 base = lua_gettop(L) - nargs, rc;

	lua_pushcfunction(L, traceback);
	lua_insert(L, base);

	current_item = item;
	deadline = get_timer() + budget_ms;
	if (depth++ == 0)
		lua_sethook(L, budget_hook, LUA_MASKCOUNT, TERMO_LUA_HOOK_COUNT);

	rc = lua_pcall(L, nargs, nresults, base);

	if (--depth == 0)
		lua_sethook(L, nullptr, 0, 0);
	deadline = saved_deadline;
	current_item = saved_item;
	lua_remove(L, base);

	if (rc == 0)
		return (0);
	report(item, lua_tostring(L, -1));
	lua_pop(L, 1);
	return (-1);
}

/* Encode the value at 1 with runtime/lua/termo/json.lua. */
static int
json_encode(lua_State *L)
{
	lua_getglobal(L, "require");
	lua_pushstring(L, "termo.json");
	lua_call(L, 1, 1);
	lua_getfield(L, -1, "encode");
	lua_pushvalue(L, 1);
	lua_call(L, 1, 1);
	return (1);
}

/* Run the loaded chunk on top of the stack; its value goes to result. */
static int
run_chunk(lua_State *L, struct cmdq_item *item, char **result, bool json)
{
	const char	*s;

	if (result != nullptr)
		*result = nullptr;
	if (termo_lua_call(L, 0, 1, TERMO_LUA_BUDGET_MS, item) != 0)
		return (-1);
	if (json) {
		lua_pushcfunction(L, json_encode);
		lua_insert(L, -2);
		if (termo_lua_call(L, 1, 1, TERMO_LUA_BUDGET_MS, item) != 0)
			return (-1);
	}
	if (result != nullptr && !lua_isnil(L, -1)) {
		lua_getglobal(L, "tostring");
		lua_insert(L, -2);
		lua_call(L, 1, 1);
		if ((s = lua_tostring(L, -1)) != nullptr)
			*result = xstrdup(s);
	}
	lua_pop(L, 1);
	return (0);
}

int
termo_lua_load_file(const char *path, struct cmdq_item *item, char **result,
    bool json)
{
	lua_State	*L = state;

	if (result != nullptr)
		*result = nullptr;
	if (luaL_loadfile(L, path) != 0) {
		report(item, lua_tostring(L, -1));
		lua_pop(L, 1);
		return (-1);
	}
	return (run_chunk(L, item, result, json));
}

int
termo_lua_eval(const char *code, struct cmdq_item *item, char **result,
    bool json)
{
	lua_State	*L = state;

	if (result != nullptr)
		*result = nullptr;
	if (luaL_loadstring(L, code) != 0) {
		report(item, lua_tostring(L, -1));
		lua_pop(L, 1);
		return (-1);
	}
	return (run_chunk(L, item, result, json));
}

static const char *
runtime_dir(void)
{
	const char	*dir = getenv("TERMO_RUNTIME");

	if (dir != nullptr && *dir != '\0')
		return (dir);
	return (TERMO_RUNTIME);
}

static void
set_package_path(lua_State *L)
{
	const char	*home = find_home(), *xdg = getenv("XDG_CONFIG_HOME");
	const char	*rt = runtime_dir(), *old;
	char		*config, *path;

	if (xdg != nullptr && *xdg != '\0')
		xasprintf(&config, "%s/termo", xdg);
	else
		xasprintf(&config, "%s/.config/termo", home != nullptr ? home : "");

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "path");
	old = lua_tostring(L, -1);
	xasprintf(&path,
	    "%s/lua/?.lua;%s/lua/?/init.lua;%s/lua/?.lua;%s/lua/?/init.lua;%s",
	    config, config, rt, rt, old != nullptr ? old : "");
	lua_pop(L, 1);
	lua_pushstring(L, path);
	lua_setfield(L, -2, "path");
	lua_pop(L, 1);

	free(path);
	free(config);
}

/*
 * Count hooks never fire inside a compiled trace, so the budget in
 * termo_lua_call only holds with the JIT engine off, and it must have no
 * off switch from Lua: these turn the engine back on, replace the hook,
 * block the loop inside a C call the hook cannot interrupt, or exit the
 * server.
 */
static const struct {
	const char	*lib;
	const char	*name;
} removed[] = {
	{ "jit", "on" },
	{ "debug", "sethook" },
	{ "os", "execute" },
	{ "io", "popen" },
	{ "os", "exit" },
};

void
termo_lua_init(void)
{
	lua_State	*L;
	u_int		 i;

	if (state != nullptr)
		return;
	if ((L = luaL_newstate()) == nullptr)
		fatalx("luaL_newstate failed");
	luaL_openlibs(L);
	set_package_path(L);

	luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
	for (i = 0; i < nitems(removed); i++) {
		lua_getglobal(L, removed[i].lib);
		lua_pushnil(L);
		lua_setfield(L, -2, removed[i].name);
		lua_pop(L, 1);
	}

	lua_pushcfunction(L, lua_print);
	lua_setglobal(L, "print");
	termo_lua_api_register(L);
	state = L;

	/* The Lua side of termo.*, if the runtime is installed or TERMO_RUNTIME set. */
	lua_getglobal(L, "pcall");
	lua_getglobal(L, "require");
	lua_pushstring(L, "termo");
	lua_call(L, 2, 0);
}

void
termo_lua_free(void)
{
	if (state == nullptr)
		return;
	termo_lua_events_free();
	termo_lua_format_free();
	lua_close(state);
	state = nullptr;
	current_item = nullptr;
	depth = 0;
}
