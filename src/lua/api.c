/*
 * termo.api: flat C functions over the core. Handles are tmux ids as
 * strings ($1, @2, %3); reading state is evaluating formats; options go
 * through the same table and parser the set-option command uses.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"

struct api_fn {
	const char	*name;
	lua_CFunction	 fn;
	const char	*signature;
	const char	*doc;
};

static int
api_version(lua_State *L)
{
	lua_pushstring(L, getversion());
	return (1);
}

/* Resolve an optional target handle, else the running command's target. */
static void
api_target(lua_State *L, int idx, struct cmd_find_state *fs)
{
	const char		*target = luaL_optstring(L, idx, nullptr);
	struct cmdq_item	*item = termo_lua_item();
	struct session		*s;
	struct window		*w;
	struct window_pane	*wp;

	if (target != nullptr) {
		if (item != nullptr) {
			if (cmd_find_target(fs, item, target, CMD_FIND_PANE,
			    CMD_FIND_QUIET) != 0)
				luaL_error(L, "no such target: %s", target);
			return;
		}
		/* No command context: ids only, resolved directly. */
		if (*target == '%' &&
		    (wp = window_pane_find_by_id_str(target)) != nullptr &&
		    cmd_find_from_pane(fs, wp, 0) == 0)
			return;
		if (*target == '@' &&
		    (w = window_find_by_id_str(target)) != nullptr &&
		    cmd_find_from_window(fs, w, 0) == 0)
			return;
		if (*target == '$' &&
		    (s = session_find_by_id_str(target)) != nullptr) {
			cmd_find_from_session(fs, s, 0);
			return;
		}
		luaL_error(L, "no such target: %s", target);
	}
	if (item != nullptr && cmd_find_valid_state(cmdq_get_target(item))) {
		cmd_find_copy_state(fs, cmdq_get_target(item));
		return;
	}
	if (cmd_find_from_nothing(fs, CMD_FIND_QUIET) != 0)
		cmd_find_clear_state(fs, 0);
}

static int
api_eval(lua_State *L)
{
	const char		*fmt = luaL_checkstring(L, 1);
	struct cmdq_item	*item = termo_lua_item();
	struct cmd_find_state	 fs;
	struct client		*c = nullptr;
	char			*s;

	api_target(L, 2, &fs);
	if (item != nullptr)
		c = cmdq_get_client(item);
	s = format_single(item, fmt, c, fs.s, fs.wl, fs.wp);
	lua_pushstring(L, s);
	free(s);
	return (1);
}

/* The options tree an option lives in: global, or the target's by scope. */
static struct options *
api_options(lua_State *L, const struct options_table_entry *oe, int target_idx)
{
	struct cmd_find_state	fs;

	if (oe->scope & OPTIONS_TABLE_SERVER)
		return (global_options);
	if (lua_isnoneornil(L, target_idx)) {
		if (oe->scope & OPTIONS_TABLE_SESSION)
			return (global_s_options);
		return (global_w_options);
	}
	api_target(L, target_idx, &fs);
	if (oe->scope & OPTIONS_TABLE_SESSION)
		return (fs.s->options);
	if ((oe->scope & OPTIONS_TABLE_PANE) && fs.wp != nullptr)
		return (fs.wp->options);
	return (fs.wl->window->options);
}

static const struct options_table_entry *
api_option_entry(lua_State *L, const char *name)
{
	const struct options_table_entry	*oe = options_search(name);

	if (oe == nullptr)
		luaL_error(L, "no such option: %s", name);
	return (oe);
}

static int
api_get_option(lua_State *L)
{
	const char				*name = luaL_checkstring(L, 1);
	const struct options_table_entry	*oe = api_option_entry(L, name);
	struct options				*oo = api_options(L, oe, 2);
	struct options_entry			*o = options_get(oo, name);
	struct options_array_item		*a;
	char					*s;

	if (o == nullptr) {
		lua_pushnil(L);
		return (1);
	}
	if (options_is_array(o)) {
		lua_newtable(L);
		for (a = options_array_first(o); a != nullptr;
		    a = options_array_next(a)) {
			s = options_to_string(o, options_array_item_key(a), 0);
			lua_pushstring(L, s);
			free(s);
			lua_setfield(L, -2, options_array_item_key(a));
		}
		return (1);
	}
	switch (oe->type) {
	case OPTIONS_TABLE_NUMBER:
		lua_pushinteger(L, options_get_number(oo, name));
		break;
	case OPTIONS_TABLE_FLAG:
		lua_pushboolean(L, options_get_number(oo, name) != 0);
		break;
	default:
		s = options_to_string(o, nullptr, 0);
		lua_pushstring(L, s);
		free(s);
		break;
	}
	return (1);
}

static int
api_set_option(lua_State *L)
{
	const char				*name = luaL_checkstring(L, 1);
	const struct options_table_entry	*oe = api_option_entry(L, name);
	struct options				*oo = api_options(L, oe, 3);
	const char				*value;
	char					*cause;

	luaL_checkany(L, 2);
	if (lua_isboolean(L, 2))
		value = lua_toboolean(L, 2) ? "on" : "off";
	else
		value = luaL_checkstring(L, 2);
	if (options_from_string(oo, oe, name, value, 0, &cause) != 0) {
		lua_pushstring(L, cause);
		free(cause);
		return (lua_error(L));
	}
	options_push_changes(name);
	return (0);
}

static int api_list(lua_State *);

static const struct api_fn api[] = {
	{ "version", api_version, "version() -> string",
	  "termo version string" },
	{ "list", api_list, "list() -> {{name, signature, doc}}",
	  "every termo.api function" },
	{ "eval", api_eval, "eval(format[, target]) -> string",
	  "expand a #{} format for a session/window/pane handle" },
	{ "get_option", api_get_option, "get_option(name[, target]) -> value",
	  "option value: number, boolean, string, or table for arrays" },
	{ "set_option", api_set_option, "set_option(name, value[, target])",
	  "set an option, parsed like set-option" },
};

static int
api_list(lua_State *L)
{
	u_int	i;

	lua_createtable(L, nitems(api), 0);
	for (i = 0; i < nitems(api); i++) {
		lua_createtable(L, 0, 3);
		lua_pushstring(L, api[i].name);
		lua_setfield(L, -2, "name");
		lua_pushstring(L, api[i].signature);
		lua_setfield(L, -2, "signature");
		lua_pushstring(L, api[i].doc);
		lua_setfield(L, -2, "doc");
		lua_rawseti(L, -2, i + 1);
	}
	return (1);
}

void
termo_lua_api_register(lua_State *L)
{
	u_int	i;

	lua_newtable(L);
	lua_newtable(L);
	for (i = 0; i < nitems(api); i++) {
		lua_pushcfunction(L, api[i].fn);
		lua_setfield(L, -2, api[i].name);
	}
	lua_setfield(L, -2, "api");
	lua_setglobal(L, "termo");
}
