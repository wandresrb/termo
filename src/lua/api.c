/*
 * termo.api: flat C functions over the core. Handles are tmux ids as
 * strings ($1, @2, %3); reading state is evaluating formats; options go
 * through the same table and parser the set-option command uses. The
 * other src/lua files add their own tables; this one registers them all.
 */

#include <sys/types.h>
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

static const struct api_fn *const groups[] = {
	termo_api_core,
	termo_api_cmd,
	termo_api_events,
	termo_api_keymap,
	termo_api_timer,
	termo_api_format,
	termo_api_ui,
};

static u_int		 held;
static struct client	*held_client;

struct client *
api_hold(struct client *c)
{
	struct client	*previous = held_client;

	held++;
	held_client = c;
	return (previous);
}

void
api_release(struct client *previous)
{
	held--;
	held_client = previous;
}

bool
api_held(void)
{
	return (held != 0);
}

struct client *
api_held_client(void)
{
	return (held_client);
}

void
api_push_handle(lua_State *L, char prefix, u_int id)
{
	lua_pushfstring(L, "%c%d", prefix, (int)id);
}

/* Parse a command string or raise its error. */
struct cmd_list *
api_parse(lua_State *L, const char *s)
{
	struct cmd_parse_result	*pr = cmd_parse_from_string(s, nullptr);

	if (pr->status == CMD_PARSE_ERROR) {
		lua_pushstring(L, pr->error);
		free(pr->error);
		lua_error(L);
	}
	return (pr->cmdlist);
}

/* Resolve an optional target handle, else the running command's target. */
void
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

/*
 * An attached client for UI calls: the one named, else the running
 * command's, else the most recently active.
 */
struct client *
api_client(lua_State *L, int idx)
{
	const char		*name = luaL_optstring(L, idx, nullptr);
	struct cmdq_item	*item = termo_lua_item();
	struct client		*c, *best = nullptr;

	if (name != nullptr) {
		TAILQ_FOREACH(c, &clients, entry) {
			if (c->session != nullptr && strcmp(c->name, name) == 0)
				return (c);
		}
		luaL_error(L, "no such client: %s", name);
	}
	if (item != nullptr) {
		c = cmdq_get_target_client(item);
		if (c == nullptr)
			c = cmdq_get_client(item);
		if (c != nullptr && c->session != nullptr)
			return (c);
	}
	TAILQ_FOREACH(c, &clients, entry) {
		if (c->session == nullptr || (c->flags & CLIENT_CONTROL))
			continue;
		if (best == nullptr ||
		    timercmp(&c->activity_time, &best->activity_time, >))
			best = c;
	}
	if (best == nullptr)
		luaL_error(L, "no client");
	return (best);
}

static int
api_version(lua_State *L)
{
	lua_pushstring(L, getversion());
	return (1);
}

static int
api_eval(lua_State *L)
{
	const char		*fmt = luaL_checkstring(L, 1);
	struct cmdq_item	*item = termo_lua_item();
	struct format_tree	*ft = termo_lua_format_tree();
	struct cmd_find_state	 fs;
	struct client		*c = nullptr;
	char			*s;

	/* Inside a format callback with no target: the tree being expanded. */
	if (ft != nullptr && lua_isnoneornil(L, 2)) {
		s = format_expand(ft, fmt);
		lua_pushstring(L, s);
		free(s);
		return (1);
	}
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
api_options(lua_State *L, const struct options_table_entry *oe, int idx)
{
	struct cmd_find_state	 fs;
	const char		*target;

	if (oe != nullptr && (oe->scope & OPTIONS_TABLE_SERVER))
		return (global_options);
	if (lua_isnoneornil(L, idx)) {
		if (oe == nullptr || (oe->scope & OPTIONS_TABLE_SESSION))
			return (global_s_options);
		return (global_w_options);
	}
	target = luaL_checkstring(L, idx);
	api_target(L, idx, &fs);
	if (oe == nullptr) {
		if (*target == '%')
			return (fs.wp->options);
		if (*target == '@')
			return (fs.wl->window->options);
		return (fs.s->options);
	}
	if (oe->scope & OPTIONS_TABLE_SESSION)
		return (fs.s->options);
	if ((oe->scope & OPTIONS_TABLE_PANE) && *target == '%')
		return (fs.wp->options);
	return (fs.wl->window->options);
}

/* Table entry for a name; user options (@x) have none. */
static const struct options_table_entry *
api_option_entry(lua_State *L, const char *name)
{
	const struct options_table_entry	*oe;

	if (*name == '@')
		return (nullptr);
	if ((oe = options_search(name)) == nullptr)
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
	switch (oe == nullptr ? OPTIONS_TABLE_STRING : oe->type) {
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

/* An array option from a table: a sequence, or {[index] = value}. */
static void
api_set_array(lua_State *L, struct options *oo,
    const struct options_table_entry *oe, const char *name)
{
	struct options_entry	*o;
	char			 key[16], *cause;
	lua_Integer		 idx;
	int			 sequence;

	if (oe == nullptr || (~oe->flags & OPTIONS_TABLE_IS_ARRAY))
		luaL_error(L, "not an array option: %s", name);
	if ((o = options_get_only(oo, name)) == nullptr)
		o = options_empty(oo, oe);
	options_array_clear(o);

	lua_rawgeti(L, 2, 1);
	sequence = !lua_isnil(L, -1);
	lua_pop(L, 1);

	lua_pushnil(L);
	while (lua_next(L, 2) != 0) {
		if (lua_type(L, -2) != LUA_TNUMBER)
			luaL_error(L, "array keys must be integers");
		idx = lua_tointeger(L, -2) - sequence;
		xsnprintf(key, sizeof key, "%lld", (long long)idx);
		if (options_array_set(o, key, luaL_checkstring(L, -1), 0,
		    &cause) != 0) {
			lua_pushstring(L, cause);
			free(cause);
			lua_error(L);
		}
		lua_pop(L, 1);
	}
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
	if (lua_istable(L, 2))
		api_set_array(L, oo, oe, name);
	else {
		if (lua_isboolean(L, 2))
			value = lua_toboolean(L, 2) ? "on" : "off";
		else
			value = luaL_checkstring(L, 2);
		if (oe == nullptr)
			options_set_string(oo, name, 0, "%s", value);
		else if (options_from_string(oo, oe, name, value, 0, &cause) != 0) {
			lua_pushstring(L, cause);
			free(cause);
			return (lua_error(L));
		}
	}
	options_push_changes(name);
	return (0);
}

static int
api_list_sessions(lua_State *L)
{
	struct session	*s;
	int		 n = 0;

	lua_newtable(L);
	RB_FOREACH(s, sessions, &sessions) {
		lua_createtable(L, 0, 4);
		api_push_handle(L, '$', s->id);
		lua_setfield(L, -2, "id");
		lua_pushstring(L, s->name);
		lua_setfield(L, -2, "name");
		lua_pushinteger(L, winlink_count(&s->windows));
		lua_setfield(L, -2, "windows");
		lua_pushboolean(L, s->attached != 0);
		lua_setfield(L, -2, "attached");
		lua_rawseti(L, -2, ++n);
	}
	return (1);
}

static int
api_list_windows(lua_State *L)
{
	struct cmd_find_state	 fs;
	struct winlink		*wl;
	int			 n = 0;

	api_target(L, 1, &fs);
	if (fs.s == nullptr)
		return (luaL_error(L, "no session"));
	lua_newtable(L);
	RB_FOREACH(wl, winlinks, &fs.s->windows) {
		lua_createtable(L, 0, 5);
		api_push_handle(L, '@', wl->window->id);
		lua_setfield(L, -2, "id");
		lua_pushinteger(L, wl->idx);
		lua_setfield(L, -2, "index");
		lua_pushstring(L, wl->window->name);
		lua_setfield(L, -2, "name");
		lua_pushboolean(L, wl == fs.s->curw);
		lua_setfield(L, -2, "active");
		lua_pushinteger(L, window_count_panes(wl->window, 1));
		lua_setfield(L, -2, "panes");
		lua_rawseti(L, -2, ++n);
	}
	return (1);
}

static int
api_list_panes(lua_State *L)
{
	struct cmd_find_state	 fs;
	struct window_pane	*wp;
	u_int			 idx;
	int			 n = 0;

	api_target(L, 1, &fs);
	if (fs.w == nullptr)
		return (luaL_error(L, "no window"));
	lua_newtable(L);
	TAILQ_FOREACH(wp, &fs.w->panes, entry) {
		lua_createtable(L, 0, 6);
		api_push_handle(L, '%', wp->id);
		lua_setfield(L, -2, "id");
		window_pane_index(wp, &idx);
		lua_pushinteger(L, idx);
		lua_setfield(L, -2, "index");
		lua_pushboolean(L, wp == fs.w->active);
		lua_setfield(L, -2, "active");
		lua_pushinteger(L, wp->sx);
		lua_setfield(L, -2, "width");
		lua_pushinteger(L, wp->sy);
		lua_setfield(L, -2, "height");
		lua_pushboolean(L, window_pane_is_floating(wp));
		lua_setfield(L, -2, "floating");
		lua_rawseti(L, -2, ++n);
	}
	return (1);
}

static int
api_fuzzy(lua_State *L)
{
	const char	*pattern = luaL_checkstring(L, 1);
	const char	*text = luaL_checkstring(L, 2);
	u_int		 width = utf8_cstrwidth(text), score = 0;
	bitstr_t	*bits;

	if (width == 0 ||
	    (bits = fuzzy_match(pattern, text, width, &score)) == nullptr) {
		lua_pushnil(L);
		return (1);
	}
	free(bits);
	lua_pushinteger(L, score);
	return (1);
}

static int api_list(lua_State *);

const struct api_fn termo_api_core[] = {
	{ "version", api_version, "version() -> string",
	  "termo version string" },
	{ "list", api_list, "list() -> {{name, signature, doc}}",
	  "every termo.api function" },
	{ "eval", api_eval, "eval(format[, target]) -> string",
	  "expand a #{} format for a session/window/pane handle; inside a "
	  "format callback with no target, against the tree being expanded" },
	{ "get_option", api_get_option, "get_option(name[, target]) -> value",
	  "option value: number, boolean, string, or table for arrays; "
	  "@names are user options" },
	{ "set_option", api_set_option, "set_option(name, value[, target])",
	  "set an option, parsed like set-option; a table sets an array" },
	{ "list_sessions", api_list_sessions,
	  "list_sessions() -> {{id, name, windows, attached}}",
	  "every session" },
	{ "list_windows", api_list_windows,
	  "list_windows([session]) -> {{id, index, name, active, panes}}",
	  "windows of a session, default the current" },
	{ "list_panes", api_list_panes,
	  "list_panes([window]) -> {{id, index, active, width, height, "
	  "floating}}",
	  "panes of a window, default the current" },
	{ "fuzzy", api_fuzzy, "fuzzy(pattern, text) -> score or nil",
	  "fzf-style match score, higher is better; nil when it does not "
	  "match" },
	{ nullptr, nullptr, nullptr, nullptr },
};

static int
api_list(lua_State *L)
{
	const struct api_fn	*f;
	u_int			 g;
	int			 n = 0;

	lua_newtable(L);
	for (g = 0; g < nitems(groups); g++) {
		for (f = groups[g]; f->name != nullptr; f++) {
			lua_createtable(L, 0, 3);
			lua_pushstring(L, f->name);
			lua_setfield(L, -2, "name");
			lua_pushstring(L, f->signature);
			lua_setfield(L, -2, "signature");
			lua_pushstring(L, f->doc);
			lua_setfield(L, -2, "doc");
			lua_rawseti(L, -2, ++n);
		}
	}
	return (1);
}

void
termo_lua_api_register(lua_State *L)
{
	const struct api_fn	*f;
	u_int			 g;

	lua_newtable(L);
	lua_newtable(L);
	for (g = 0; g < nitems(groups); g++) {
		for (f = groups[g]; f->name != nullptr; f++) {
			lua_pushcfunction(L, f->fn);
			lua_setfield(L, -2, f->name);
		}
	}
	lua_setfield(L, -2, "api");
	lua_setglobal(L, "termo");
}
