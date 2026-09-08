/*
 * termo.api.format_add: #{name} variables computed in Lua. Every new
 * format tree gets a lazy entry per registered name, so a variable
 * nothing references costs nothing; the callback runs with the tree
 * current, so eval() inside it reads the same target.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

static const char FORMATS[] = "termo.formats";

static char			**names;
static u_int			  nnames;
static struct format_tree	 *current;
static u_int			  depth;

struct format_tree *
termo_lua_format_tree(void)
{
	return (current);
}

static void
names_free(void)
{
	u_int	i;

	for (i = 0; i < nnames; i++)
		free(names[i]);
	free(names);
	names = nullptr;
	nnames = 0;
}

void
termo_lua_format_free(void)
{
	names_free();
	current = nullptr;
	depth = 0;
}

static void *
lua_format_cb(struct format_tree *ft)
{
	lua_State		*L = termo_lua_state();
	const char		*key = format_cb_key(ft), *s;
	struct format_tree	*saved = current;
	char			*value;
	int			 rc;

	if (L == nullptr || depth >= 4)
		return (xstrdup(""));
	lua_getfield(L, LUA_REGISTRYINDEX, FORMATS);
	lua_getfield(L, -1, key);
	lua_remove(L, -2);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return (xstrdup(""));
	}
	lua_pushstring(L, key);

	current = ft;
	depth++;
	rc = termo_lua_call(L, 1, 1, TERMO_LUA_BUDGET_CB_MS, nullptr);
	depth--;
	current = saved;

	if (rc != 0)
		return (xstrdup(""));
	s = lua_tostring(L, -1);
	value = xstrdup(s != nullptr ? s : "");
	lua_pop(L, 1);
	return (value);
}

void
termo_lua_format_register(struct format_tree *ft)
{
	u_int	i;

	for (i = 0; i < nnames; i++)
		format_add_cb(ft, names[i], lua_format_cb);
}

static int
api_format_add(lua_State *L)
{
	const char	*name = luaL_checkstring(L, 1), *cp;
	u_int		 i;

	for (cp = name; *cp != '\0'; cp++) {
		if (!isalnum((u_char)*cp) && *cp != '_')
			return (luaL_error(L, "bad variable name: %s", name));
	}
	if (cp == name)
		return (luaL_error(L, "empty variable name"));
	if (!lua_isnoneornil(L, 2))
		luaL_checktype(L, 2, LUA_TFUNCTION);

	lua_getfield(L, LUA_REGISTRYINDEX, FORMATS);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, FORMATS);
	}
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, name);

	names_free();
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		lua_pop(L, 1);
		nnames++;
	}
	names = xreallocarray(nullptr, nnames == 0 ? 1 : nnames, sizeof *names);
	i = 0;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		lua_pop(L, 1);
		names[i++] = xstrdup(lua_tostring(L, -1));
	}
	return (0);
}

const struct api_fn termo_api_format[] = {
	{ "format_add", api_format_add, "format_add(name, fn(name) or nil)",
	  "define #{name} for status lines and formats; fn returns the value "
	  "and may call eval() for the target being drawn; nil removes it" },
	{ nullptr, nullptr, nullptr, nullptr },
};
