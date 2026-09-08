/*
 * termo.api.menu, popup, message and prompt over the existing overlays.
 * Each needs an attached client: the one named, the running command's,
 * or the most recently active. Handlers live in one registry table per
 * overlay and are released from its close or free callback.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

struct lua_ui {
	int		 ref;
	struct client	*c;
};

/* Take the table on top of the stack as the handlers of a new overlay. */
static struct lua_ui *
ui_new(lua_State *L, struct client *c)
{
	struct lua_ui	*u;

	u = xcalloc(1, sizeof *u);
	u->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	u->c = c;
	c->references++;
	return (u);
}

static void
ui_free(struct lua_ui *u)
{
	lua_State	*L = termo_lua_state();

	if (L != nullptr)
		luaL_unref(L, LUA_REGISTRYINDEX, u->ref);
	server_client_unref(u->c);
	free(u);
}

/* The client named in spec.client, or the default. */
static struct client *
ui_client(lua_State *L, int spec)
{
	struct client	*c;

	lua_getfield(L, spec, "client");
	c = api_client(L, lua_gettop(L));
	lua_pop(L, 1);
	return (c);
}

static const char *
ui_string(lua_State *L, int spec, const char *field, const char *dflt)
{
	const char	*s;

	lua_getfield(L, spec, field);
	s = lua_tostring(L, -1);
	lua_pop(L, 1);	/* still referenced by the spec table */
	return (s != nullptr ? s : dflt);
}

static u_int
ui_number(lua_State *L, int spec, const char *field, u_int dflt)
{
	u_int	n = dflt;

	lua_getfield(L, spec, field);
	if (lua_isnumber(L, -1))
		n = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return (n);
}

/* Call handlers[field](args already on the stack), or drop the args. */
static void
ui_call(lua_State *L, struct lua_ui *u, const char *field, int nargs)
{
	struct client	*held;

	lua_rawgeti(L, LUA_REGISTRYINDEX, u->ref);
	lua_getfield(L, -1, field);
	lua_remove(L, -2);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, nargs + 1);
		return;
	}
	lua_insert(L, -nargs - 1);
	held = api_hold(u->c);
	termo_lua_call(L, nargs, 0, TERMO_LUA_BUDGET_MS, nullptr);
	api_release(held);
}

static void
menu_cb(struct menu *menu, u_int choice, key_code key, void *data)
{
	struct lua_ui		*u = data;
	lua_State		*L = termo_lua_state();
	struct client		*held;
	struct cmd_parse_result	*pr;
	struct cmdq_state	*state;
	struct cmd_find_state	 fs;

	if (L == nullptr) {
		ui_free(u);
		return;
	}
	if (choice == UINT_MAX || choice >= menu->count) {
		ui_call(L, u, "on_close", 0);
		ui_free(u);
		return;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, u->ref);
	lua_rawgeti(L, -1, choice + 1);
	if (lua_isfunction(L, -1)) {
		lua_pushinteger(L, choice + 1);
		lua_pushstring(L, key_string_lookup_key(key, 0));
		held = api_hold(u->c);
		termo_lua_call(L, 2, 0, TERMO_LUA_BUDGET_MS, nullptr);
		api_release(held);
	} else if (lua_isstring(L, -1)) {
		pr = cmd_parse_from_string(lua_tostring(L, -1), nullptr);
		if (pr->status == CMD_PARSE_ERROR) {
			cmdq_append(u->c, cmdq_get_error(pr->error));
			free(pr->error);
		} else {
			cmd_find_from_client(&fs, u->c, 0);
			state = cmdq_new_state(&fs, nullptr, 0);
			cmdq_append(u->c, cmdq_get_command(pr->cmdlist, state));
			cmdq_free_state(state);
			cmd_list_free(pr->cmdlist);
		}
		lua_pop(L, 1);
	} else
		lua_pop(L, 1);
	lua_pop(L, 1);
	ui_free(u);
}

/*
 * menu{title, items = {{name, key, fn or command}, ...}, x, y, client,
 * on_close}. An item with no name is a separator.
 */
static int
api_menu(lua_State *L)
{
	struct cmdq_item	*item = termo_lua_item();
	struct client		*c;
	struct menu		*menu;
	struct menu_item	 mi;
	struct cmd_find_state	 fs;
	struct lua_ui		*u;
	const char		*name, *keystr;
	u_int			 px, py, i, n;
	int			 flags = MENU_NOMOUSE;

	luaL_checktype(L, 1, LUA_TTABLE);
	c = ui_client(L, 1);
	lua_getfield(L, 1, "items");
	if (!lua_istable(L, -1))
		return (luaL_error(L, "menu needs an items table"));
	if (item != nullptr && cmd_find_valid_state(cmdq_get_target(item)))
		cmd_find_copy_state(&fs, cmdq_get_target(item));
	else
		cmd_find_from_client(&fs, c, 0);

	menu = menu_create(ui_string(L, 1, "title", ""));
	lua_newtable(L);	/* handlers: [index] = fn or command */
	n = lua_objlen(L, -2);
	for (i = 1; i <= n; i++) {
		lua_rawgeti(L, -2, i);
		if (lua_istable(L, -1)) {
			lua_rawgeti(L, -1, 1);
			if (lua_isnil(L, -1)) {
				lua_pop(L, 1);
				lua_getfield(L, -1, "name");
			}
			name = lua_tostring(L, -1);
			lua_rawgeti(L, -2, 2);
			if (lua_isnil(L, -1)) {
				lua_pop(L, 1);
				lua_getfield(L, -2, "key");
			}
			keystr = lua_tostring(L, -1);
			lua_rawgeti(L, -3, 3);
			if (lua_isnil(L, -1)) {
				lua_pop(L, 1);
				lua_getfield(L, -3, "fn");
			}
			if (lua_isnil(L, -1)) {
				lua_pop(L, 1);
				lua_getfield(L, -3, "cmd");
			}
		} else {
			name = lua_tostring(L, -1);
			lua_pushnil(L);
			lua_pushnil(L);
			keystr = nullptr;
		}
		/* stack: items, handlers, entry, name, key, action */
		if (name == nullptr || *name == '\0')
			menu_add_item(menu, nullptr, item, c, &fs);
		else {
			mi.name = name;
			mi.key = KEYC_NONE;
			if (keystr != nullptr) {
				mi.key = key_string_lookup_string(keystr);
				if (mi.key == KEYC_UNKNOWN) {
					menu_free(menu);
					return (luaL_error(L, "unknown key: %s",
					    keystr));
				}
			}
			mi.command = lua_isstring(L, -1) ? lua_tostring(L, -1) :
			    nullptr;
			menu_add_item(menu, &mi, item, c, &fs);
			lua_pushvalue(L, -1);
			lua_rawseti(L, -6, menu->count);
		}
		lua_pop(L, 4);
	}
	if (menu->count == 0) {
		menu_free(menu);
		return (luaL_error(L, "empty menu"));
	}
	lua_getfield(L, 1, "on_close");
	lua_setfield(L, -2, "on_close");

	px = ui_number(L, 1, "x", c->tty.sx > menu->width + 4 ?
	    (c->tty.sx - menu->width - 4) / 2 : 0);
	py = ui_number(L, 1, "y", c->tty.sy > menu->count + 2 ?
	    (c->tty.sy - menu->count - 2) / 2 : 0);
	if (item != nullptr && cmdq_get_event(item)->m.valid)
		flags = 0;

	u = ui_new(L, c);
	if (menu_display(menu, flags, 0, item, px, py, c, BOX_LINES_DEFAULT,
	    nullptr, nullptr, nullptr, &fs, menu_cb, u) != 0) {
		menu_free(menu);
		ui_free(u);
		return (luaL_error(L, "cannot display menu"));
	}
	return (0);
}

static void
popup_cb(int status, void *arg)
{
	struct lua_ui	*u = arg;
	lua_State	*L = termo_lua_state();

	if (L != nullptr) {
		lua_pushinteger(L, status);
		ui_call(L, u, "on_close", 1);
	}
	ui_free(u);
}

/*
 * popup{cmd = string or argv, w, h, x, y, title, cwd, close = "exit" |
 * "any" | "manual", on_close = fn(status), client}.
 */
static int
api_popup(lua_State *L)
{
	struct client	*c;
	struct session	*s;
	struct lua_ui	*u;
	const char	*shellcmd = nullptr, *cwd, *title, *close;
	char		**argv = nullptr;
	int		 argc = 0, flags = POPUP_CLOSEEXIT, i, n;
	u_int		 w, h, px, py;

	luaL_checktype(L, 1, LUA_TTABLE);
	c = ui_client(L, 1);
	s = c->session;

	w = ui_number(L, 1, "w", c->tty.sx / 2);
	h = ui_number(L, 1, "h", c->tty.sy / 2);
	if (w > c->tty.sx)
		w = c->tty.sx;
	if (h > c->tty.sy)
		h = c->tty.sy;
	px = ui_number(L, 1, "x", (c->tty.sx - w) / 2);
	py = ui_number(L, 1, "y", (c->tty.sy - h) / 2);

	close = ui_string(L, 1, "close", "exit");
	if (strcmp(close, "any") == 0)
		flags = POPUP_CLOSEANYKEY;
	else if (strcmp(close, "manual") == 0)
		flags = 0;
	else if (strcmp(close, "exit-zero") == 0)
		flags = POPUP_CLOSEEXITZERO;
	else if (strcmp(close, "exit") != 0)
		return (luaL_error(L, "bad close mode: %s", close));

	lua_getfield(L, 1, "cmd");
	if (lua_type(L, -1) == LUA_TSTRING)
		shellcmd = lua_tostring(L, -1);
	else if (lua_istable(L, -1)) {
		n = lua_objlen(L, -1);
		for (i = 1; i <= n; i++) {
			lua_rawgeti(L, -1, i);
			if (lua_type(L, -1) != LUA_TSTRING) {
				cmd_free_argv(argc, argv);
				return (luaL_error(L, "cmd must be strings"));
			}
			cmd_append_argv(&argc, &argv, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
	if (shellcmd == nullptr && argc == 0)
		shellcmd = options_get_string(s->options, "default-command");
	if (shellcmd != nullptr && *shellcmd == '\0') {
		shellcmd = nullptr;
		cmd_append_argv(&argc, &argv,
		    options_get_string(s->options, "default-shell"));
	}
	cwd = ui_string(L, 1, "cwd", server_client_get_cwd(c, s));
	title = ui_string(L, 1, "title", "");

	lua_getfield(L, 1, "on_close");
	lua_createtable(L, 0, 1);
	lua_insert(L, -2);
	lua_setfield(L, -2, "on_close");
	u = ui_new(L, c);

	if (popup_display(flags, BOX_LINES_DEFAULT, nullptr, px, py, w, h,
	    nullptr, shellcmd, argc, argv, cwd, title, c, s, nullptr, nullptr,
	    popup_cb, u) != 0) {
		cmd_free_argv(argc, argv);
		ui_free(u);
		return (luaL_error(L, "cannot display popup"));
	}
	cmd_free_argv(argc, argv);
	return (0);
}

static int
api_message(lua_State *L)
{
	const char	*s = luaL_checkstring(L, 1);
	struct client	*c = api_client(L, 2);

	status_message_set(c, -1, 1, 0, 0, "%s", s);
	return (0);
}

static enum prompt_result
lua_prompt_cb([[maybe_unused]] struct client *c, void *data, const char *s,
    enum prompt_key_result key)
{
	struct lua_ui	*u = data;
	lua_State	*L = termo_lua_state();

	if (L == nullptr)
		return (PROMPT_CLOSE);
	if (s == nullptr) {
		lua_pushnil(L);
		lua_pushboolean(L, 1);
		ui_call(L, u, "fn", 2);
		return (PROMPT_CLOSE);
	}
	switch (key) {
	case PROMPT_KEY_CLOSE:
		lua_pushstring(L, s);
		lua_pushboolean(L, 1);
		ui_call(L, u, "fn", 2);
		return (PROMPT_CLOSE);
	case PROMPT_KEY_MOVE:
		lua_pushstring(L, s);
		lua_pushstring(L, "move");
		ui_call(L, u, "fn", 2);
		return (PROMPT_CONTINUE);
	default:
		/* Incremental updates carry a one-character prefix. */
		if (*s == '=' || *s == '+' || *s == '-')
			s++;
		lua_pushstring(L, s);
		lua_pushboolean(L, 0);
		ui_call(L, u, "fn", 2);
		return (PROMPT_CONTINUE);
	}
}

static void
lua_prompt_free(void *data)
{
	ui_free(data);
}

/* prompt(label, fn(text, done), {input, incremental, single, client}) */
static int
api_prompt(lua_State *L)
{
	const char		*label = luaL_checkstring(L, 1), *input = nullptr;
	struct client		*c;
	struct cmd_find_state	 fs;
	struct lua_ui		*u;
	int			 flags = 0;

	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (!lua_isnoneornil(L, 3)) {
		luaL_checktype(L, 3, LUA_TTABLE);
		c = ui_client(L, 3);
		input = ui_string(L, 3, "input", nullptr);
		lua_getfield(L, 3, "incremental");
		if (lua_toboolean(L, -1))
			flags |= PROMPT_INCREMENTAL;
		lua_getfield(L, 3, "single");
		if (lua_toboolean(L, -1))
			flags |= PROMPT_SINGLE;
		lua_pop(L, 2);
	} else
		c = api_client(L, 3);
	cmd_find_from_client(&fs, c, 0);

	lua_createtable(L, 0, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "fn");
	u = ui_new(L, c);
	status_prompt_set(c, &fs, label, input, lua_prompt_cb, lua_prompt_free, u,
	    flags, PROMPT_TYPE_COMMAND);
	return (0);
}

const struct api_fn termo_api_ui[] = {
	{ "menu", api_menu,
	  "menu{title, items = {{name, key, fn(index, key) or command}...}, "
	  "x, y, on_close, client}",
	  "show a menu; an item without a name is a separator" },
	{ "popup", api_popup,
	  "popup{cmd = string or argv, w, h, x, y, title, cwd, close = "
	  "\"exit\"|\"exit-zero\"|\"any\"|\"manual\", on_close = fn(status), "
	  "client}",
	  "run a command in a popup window" },
	{ "message", api_message, "message(text[, client])",
	  "show a message in the status line" },
	{ "prompt", api_prompt,
	  "prompt(label, fn(text, done), {input, incremental, single, client})",
	  "ask for input in the status line; fn gets nil when cancelled, "
	  "done is true on enter, false for incremental updates, \"move\" "
	  "for up/down" },
	{ nullptr, nullptr, nullptr, nullptr },
};
