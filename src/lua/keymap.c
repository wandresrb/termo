/*
 * termo.api.keymap_set and keymap_del: bindings whose right-hand side is
 * a Lua function. The binding is the ordinary command "run-lua -r <ref>",
 * so the key dispatcher does not know Lua exists; the ref points at a
 * registry table {fn, table} and run-lua calls fn with the key event.
 */

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

/* Registry: table name -> { key string -> ref }, to unref on rebind. */
static const char KEYMAPS[] = "termo.keymaps";

static void
keymap_table(lua_State *L, const char *tname)
{
	lua_getfield(L, LUA_REGISTRYINDEX, KEYMAPS);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, KEYMAPS);
	}
	lua_getfield(L, -1, tname);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, tname);
	}
	lua_remove(L, -2);
}

/* Drop the function bound at a key, if any, and leave the table on top. */
static void
keymap_forget(lua_State *L, const char *tname, key_code key)
{
	const char	*keystr = key_string_lookup_key(key, 0);

	keymap_table(L, tname);
	lua_getfield(L, -1, keystr);
	if (lua_isnumber(L, -1))
		luaL_unref(L, LUA_REGISTRYINDEX, lua_tointeger(L, -1));
	lua_pop(L, 1);
	lua_pushnil(L);
	lua_setfield(L, -2, keystr);
}

static int
api_keymap_set(lua_State *L)
{
	const char		*tname = luaL_checkstring(L, 1);
	const char		*keystr = luaL_checkstring(L, 2);
	const char		*note = nullptr;
	key_code		 key = key_string_lookup_string(keystr);
	struct cmd_parse_result	*pr;
	struct cmd_list		*cmdlist;
	char			*s;
	int			 repeat = 0, ref;

	if (key == KEYC_UNKNOWN)
		return (luaL_error(L, "unknown key: %s", keystr));
	if (lua_type(L, 3) != LUA_TSTRING)
		luaL_checktype(L, 3, LUA_TFUNCTION);
	if (!lua_isnoneornil(L, 4)) {
		luaL_checktype(L, 4, LUA_TTABLE);
		lua_getfield(L, 4, "repeat");
		repeat = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 4, "note");
		note = lua_tostring(L, -1);	/* stays on the stack */
	}

	if (lua_type(L, 3) == LUA_TSTRING)
		cmdlist = api_parse(L, lua_tostring(L, 3));
	else {
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, 3);
		lua_setfield(L, -2, "fn");
		lua_pushvalue(L, 1);
		lua_setfield(L, -2, "table");
		ref = luaL_ref(L, LUA_REGISTRYINDEX);

		xasprintf(&s, "run-lua -r %d", ref);
		pr = cmd_parse_from_string(s, nullptr);
		free(s);
		cmdlist = pr->cmdlist;
	}
	keymap_forget(L, tname, key);
	if (lua_type(L, 3) != LUA_TSTRING) {
		lua_pushinteger(L, ref);
		lua_setfield(L, -2, key_string_lookup_key(key, 0));
	}
	key_bindings_add(tname, key, note, repeat, cmdlist);
	return (0);
}

static int
api_keymap_del(lua_State *L)
{
	const char	*tname = luaL_checkstring(L, 1);
	const char	*keystr = luaL_checkstring(L, 2);
	key_code	 key = key_string_lookup_string(keystr);

	if (key == KEYC_UNKNOWN)
		return (luaL_error(L, "unknown key: %s", keystr));
	keymap_forget(L, tname, key);
	key_bindings_remove(tname, key);
	return (0);
}

/* run-lua -r: call the bound function with the key event. */
int
termo_lua_keymap_run(struct cmdq_item *item, int ref, struct args *args)
{
	lua_State		*L = termo_lua_state();
	struct key_event	*event = cmdq_get_event(item);
	struct client		*c = cmdq_get_client(item);
	u_int			 i, n = args_count(args);
	int			 rc;

	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		cmdq_error(item, "no such key handler: %d", ref);
		return (-1);
	}
	lua_getfield(L, -1, "fn");
	lua_createtable(L, 0, 4);
	if (c != nullptr) {
		lua_pushstring(L, c->name);
		lua_setfield(L, -2, "client");
	}
	if (event->key != KEYC_NONE) {
		lua_pushstring(L, key_string_lookup_key(event->key, 0));
		lua_setfield(L, -2, "key");
	}
	lua_getfield(L, -3, "table");
	lua_setfield(L, -2, "table");
	lua_createtable(L, (int)n, 0);
	for (i = 0; i < n; i++) {
		lua_pushstring(L, args_string(args, i));
		lua_rawseti(L, -2, (int)i + 1);
	}
	lua_setfield(L, -2, "args");
	if (event->m.valid) {
		lua_createtable(L, 0, 3);
		lua_pushinteger(L, event->m.x);
		lua_setfield(L, -2, "x");
		lua_pushinteger(L, event->m.y);
		lua_setfield(L, -2, "y");
		lua_pushinteger(L, event->m.b);
		lua_setfield(L, -2, "b");
		lua_setfield(L, -2, "mouse");
	}
	rc = termo_lua_call(L, 1, 0, TERMO_LUA_BUDGET_MS, item);
	lua_pop(L, 1);
	return (rc);
}

static const char COMMANDS[] = "termo.commands";

static void
command_forget(lua_State *L, const char *name)
{
	lua_getfield(L, LUA_REGISTRYINDEX, COMMANDS);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, COMMANDS);
	}
	lua_getfield(L, -1, name);
	if (lua_isnumber(L, -1))
		luaL_unref(L, LUA_REGISTRYINDEX, lua_tointeger(L, -1));
	lua_pushboolean(L, lua_isnumber(L, -1));
	lua_remove(L, -2);
	lua_pushnil(L);
	lua_setfield(L, -3, name);
}

static int
api_command_set(lua_State *L)
{
	const char	*name = luaL_checkstring(L, 1), *p;
	char		*value, *cause = nullptr;
	int		 ref;

	luaL_checktype(L, 2, LUA_TFUNCTION);
	for (p = name; *p != '\0'; p++) {
		if (!isalnum((u_char)*p) && *p != '-' && *p != '_')
			return (luaL_error(L, "bad command name: %s", name));
	}
	if (*name == '\0' || isdigit((u_char)*name))
		return (luaL_error(L, "bad command name: %s", name));
	if (cmd_find(name, &cause) != nullptr) {
		free(cause);
		return (luaL_error(L, "command exists: %s", name));
	}
	free(cause);

	command_forget(L, name);
	lua_createtable(L, 0, 2);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "fn");
	lua_pushstring(L, "command");
	lua_setfield(L, -2, "table");
	ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushinteger(L, ref);
	lua_setfield(L, -3, name);
	lua_pop(L, 2);

	xasprintf(&value, "%s=run-lua -r %d", name, ref);
	if (options_array_set(options_get_only(global_options, "command-alias"),
	    name, value, 0, &cause) != 0) {
		free(value);
		lua_pushstring(L, cause);
		free(cause);
		return (lua_error(L));
	}
	free(value);
	return (0);
}

static int
api_command_del(lua_State *L)
{
	const char	*name = luaL_checkstring(L, 1);
	bool		 had;

	command_forget(L, name);
	had = lua_toboolean(L, -1);
	lua_pop(L, 2);
	options_array_set(options_get_only(global_options, "command-alias"),
	    name, nullptr, 0, nullptr);
	lua_pushboolean(L, had);
	return (1);
}

const struct api_fn termo_api_keymap[] = {
	{ "keymap_set", api_keymap_set,
	  "keymap_set(table, key, command or fn(event)[, {repeat, note}])",
	  "bind a key in a key table (prefix, root, copy-mode-vi...) to "
	  "commands or to a function called with {client, key, table, mouse}" },
	{ "keymap_del", api_keymap_del, "keymap_del(table, key)",
	  "remove a binding" },
	{ "command_set", api_command_set, "command_set(name, fn(event))",
	  "define a command run from the : prompt, bind-key, control mode "
	  "or the CLI as `name [argument ...]`; fn gets {client, args}" },
	{ "command_del", api_command_del, "command_del(name) -> removed",
	  "remove a command defined with command_set" },
	{ nullptr, nullptr, nullptr, nullptr },
};
