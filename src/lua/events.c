/*
 * termo.api.on, off and emit: Lua on the event bus. One C sink per event
 * name; the handlers live in the registry as name -> {{id, fn}, ...} and
 * an id -> name table for off(). Callbacks run on the stack of whatever
 * fired the event, so while one runs cmd() only queues (see cmd.c).
 */

#include <sys/types.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

struct lua_sink {
	char			*name;
	struct events_sink	*sink;
	TAILQ_ENTRY(lua_sink)	 entry;
};
static TAILQ_HEAD(, lua_sink) sinks = TAILQ_HEAD_INITIALIZER(sinks);

static const char HANDLERS[] = "termo.events";
static const char IDS[] = "termo.events.ids";

static int	next_id;

/* Push a registry table, creating it the first time. */
static void
push_registry_table(lua_State *L, const char *name)
{
	lua_getfield(L, LUA_REGISTRYINDEX, name);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, name);
	}
}

static void
payload_to_table(lua_State *L, struct event_payload *ep)
{
	struct event_payload_item	*epi;
	const char			*name;
	int				 i;
	u_int				 u;

	lua_newtable(L);
	for (epi = event_payload_first(ep); epi != nullptr;
	    epi = event_payload_next(epi)) {
		name = event_payload_item_name(epi);
		switch (event_payload_item_type(epi)) {
		case EVENT_PAYLOAD_STRING:
			lua_pushstring(L, event_payload_get_string(ep, name));
			break;
		case EVENT_PAYLOAD_TIME:
			lua_pushnumber(L, event_payload_get_time(ep, name));
			break;
		case EVENT_PAYLOAD_INT:
			event_payload_get_int(ep, name, &i);
			lua_pushinteger(L, i);
			break;
		case EVENT_PAYLOAD_UINT:
			event_payload_get_uint(ep, name, &u);
			lua_pushinteger(L, u);
			break;
		case EVENT_PAYLOAD_CLIENT:
			lua_pushstring(L,
			    event_payload_get_client(ep, name)->name);
			break;
		case EVENT_PAYLOAD_SESSION:
			api_push_handle(L, '$',
			    event_payload_get_session(ep, name)->id);
			break;
		case EVENT_PAYLOAD_WINDOW:
			api_push_handle(L, '@',
			    event_payload_get_window(ep, name)->id);
			break;
		case EVENT_PAYLOAD_PANE:
			api_push_handle(L, '%',
			    event_payload_get_pane(ep, name)->id);
			break;
		case EVENT_PAYLOAD_POINTER:
			continue;
		}
		lua_setfield(L, -2, name);
	}
}

static void
lua_sink_cb(const char *name, struct event_payload *ep,
    [[maybe_unused]] void *data)
{
	lua_State	*L = termo_lua_state();
	struct client	*held;
	int		 top = lua_gettop(L), n, i;

	push_registry_table(L, HANDLERS);
	lua_getfield(L, -1, name);
	if (lua_isnil(L, -1)) {
		lua_settop(L, top);
		return;
	}
	lua_remove(L, -2);
	n = lua_objlen(L, -1);
	payload_to_table(L, ep);

	/*
	 * Handlers added while dispatching are past n, removed ones are
	 * false: a callback cannot upset the walk.
	 */
	held = api_hold(api_held_client());
	for (i = 1; i <= n; i++) {
		lua_rawgeti(L, -2, i);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			continue;
		}
		lua_getfield(L, -1, "fn");
		lua_remove(L, -2);
		lua_pushvalue(L, -2);
		termo_lua_call(L, 1, 0, TERMO_LUA_BUDGET_CB_MS, nullptr);
	}
	api_release(held);
	lua_settop(L, top);
}

static struct lua_sink *
sink_find(const char *name)
{
	struct lua_sink	*ls;

	TAILQ_FOREACH(ls, &sinks, entry) {
		if (strcmp(ls->name, name) == 0)
			return (ls);
	}
	return (nullptr);
}

static void
sink_remove(struct lua_sink *ls)
{
	TAILQ_REMOVE(&sinks, ls, entry);
	events_remove_sink(ls->sink);
	free(ls->name);
	free(ls);
}

void
termo_lua_events_free(void)
{
	struct lua_sink	*ls;

	while ((ls = TAILQ_FIRST(&sinks)) != nullptr)
		sink_remove(ls);
	next_id = 0;
}

static int
api_on(lua_State *L)
{
	const char	*name = luaL_checkstring(L, 1);
	struct lua_sink	*ls;
	int		 id;

	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (!hooks_valid_event_name(name))
		return (luaL_error(L, "no such event: %s", name));

	push_registry_table(L, HANDLERS);
	lua_getfield(L, -1, name);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, name);

		ls = xcalloc(1, sizeof *ls);
		ls->name = xstrdup(name);
		ls->sink = events_add_sink(name, lua_sink_cb, nullptr);
		TAILQ_INSERT_TAIL(&sinks, ls, entry);
	}

	id = ++next_id;
	lua_createtable(L, 0, 2);
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "fn");
	lua_rawseti(L, -2, lua_objlen(L, -2) + 1);

	push_registry_table(L, IDS);
	lua_pushstring(L, name);
	lua_rawseti(L, -2, id);

	lua_pushinteger(L, id);
	return (1);
}

static int
api_off(lua_State *L)
{
	int		 id = luaL_checkinteger(L, 1), n, i, live = 0;
	const char	*name;
	struct lua_sink	*ls;

	push_registry_table(L, IDS);
	lua_rawgeti(L, -1, id);
	if (lua_isnil(L, -1)) {
		lua_pushboolean(L, 0);
		return (1);
	}
	name = lua_tostring(L, -1);
	lua_pushnil(L);
	lua_rawseti(L, -3, id);

	push_registry_table(L, HANDLERS);
	lua_getfield(L, -1, name);
	n = lua_objlen(L, -1);
	for (i = 1; i <= n; i++) {
		lua_rawgeti(L, -1, i);
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "id");
			if (lua_tointeger(L, -1) == id) {
				lua_pushboolean(L, 0);
				lua_rawseti(L, -4, i);
			} else
				live++;
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	if (live == 0) {
		lua_pushnil(L);
		lua_setfield(L, -3, name);
		if ((ls = sink_find(name)) != nullptr)
			sink_remove(ls);
	}
	lua_pushboolean(L, 1);
	return (1);
}

/* Everything in the table must be a string, number or boolean. */
static void
check_payload_table(lua_State *L, int idx)
{
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		if (lua_type(L, -2) != LUA_TSTRING)
			luaL_error(L, "payload keys must be strings");
		switch (lua_type(L, -1)) {
		case LUA_TSTRING:
		case LUA_TNUMBER:
		case LUA_TBOOLEAN:
			break;
		default:
			luaL_error(L, "payload value for %s must be a string, "
			    "number or boolean", lua_tostring(L, -2));
		}
		lua_pop(L, 1);
	}
}

static int
api_emit(lua_State *L)
{
	const char		*name = luaL_checkstring(L, 1);
	struct cmdq_item	*item = termo_lua_item();
	struct event_payload	*ep;
	lua_Number		 d;

	if (!hooks_valid_event_name(name))
		return (luaL_error(L, "no such event: %s", name));
	if (!lua_isnoneornil(L, 2)) {
		luaL_checktype(L, 2, LUA_TTABLE);
		check_payload_table(L, 2);
	}

	ep = event_payload_create();
	if (item != nullptr && cmd_find_valid_state(cmdq_get_target(item)))
		event_payload_set_target(ep, cmdq_get_target(item));
	if (lua_istable(L, 2)) {
		lua_pushnil(L);
		while (lua_next(L, 2) != 0) {
			switch (lua_type(L, -1)) {
			case LUA_TSTRING:
				event_payload_set_string(ep, lua_tostring(L, -2),
				    "%s", lua_tostring(L, -1));
				break;
			case LUA_TNUMBER:
				d = lua_tonumber(L, -1);
				if (d == floor(d) && fabs(d) < 2147483648.0) {
					event_payload_set_int(ep,
					    lua_tostring(L, -2), (int)d);
				} else {
					event_payload_set_string(ep,
					    lua_tostring(L, -2), "%.14g", d);
				}
				break;
			default:
				event_payload_set_int(ep, lua_tostring(L, -2),
				    lua_toboolean(L, -1));
				break;
			}
			lua_pop(L, 1);
		}
	}
	events_fire(name, ep);
	return (0);
}

const struct api_fn termo_api_events[] = {
	{ "on", api_on, "on(event, fn(payload)) -> id",
	  "call fn with a table of the payload each time event fires; "
	  "any hook name, or a @custom one" },
	{ "off", api_off, "off(id) -> boolean",
	  "remove a handler returned by on" },
	{ "emit", api_emit, "emit(event[, payload])",
	  "fire an event with a table of strings, numbers and booleans; "
	  "hooks and on() handlers see it" },
	{ nullptr, nullptr, nullptr, nullptr },
};
