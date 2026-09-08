/*
 * termo.api.cmd and cmd_async: commands from Lua go through the command
 * queue like everything else. Where they go depends on what is running,
 * the same rule hooks follow: after the running command, after the core
 * operation that fired an event, or drained on the spot from a timer.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

struct lua_cmd {
	struct cmdq_state	*state;
	struct evbuffer		*out;
	struct evbuffer		*err;
	int			 ref;
	bool			 started;
	bool			 done;
	bool			 orphan;	/* the callback item frees it */
};

static enum cmd_retval
lua_cmd_start([[maybe_unused]] struct cmdq_item *item, void *data)
{
	struct lua_cmd	*lc = data;

	lc->started = true;
	return (CMD_RETURN_NORMAL);
}

/*
 * Inside a running command, consecutive cmd() calls must run in order:
 * each one goes after the last one inserted, not straight after the
 * command. The anchor is valid while that command is still running.
 */
static struct cmdq_item	*anchor_item;
static struct cmdq_item	*anchor_last;

static struct cmdq_item *
lua_cmd_insert(struct cmdq_item *item, struct cmdq_item *first)
{
	struct cmdq_item	*after = item;

	if (anchor_item == item)
		after = anchor_last;
	anchor_item = item;
	anchor_last = cmdq_insert_after(after, first);
	return (anchor_last);
}

static void
lua_cmd_free(struct lua_cmd *lc)
{
	cmdq_free_state(lc->state);
	evbuffer_free(lc->out);
	evbuffer_free(lc->err);
	free(lc);
}

/* Captured output without its final newline, or nil when empty. */
static void
lua_cmd_push_buffer(lua_State *L, struct evbuffer *evb)
{
	size_t	n = EVBUFFER_LENGTH(evb);

	if (n == 0) {
		lua_pushnil(L);
		return;
	}
	lua_pushlstring(L, (const char *)EVBUFFER_DATA(evb), n - 1);
}

static enum cmd_retval
lua_cmd_done(struct cmdq_item *item, void *data)
{
	struct lua_cmd	*lc = data;
	lua_State	*L = termo_lua_state();

	lc->done = true;
	if (lc->ref != LUA_NOREF && L != nullptr) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, lc->ref);
		luaL_unref(L, LUA_REGISTRYINDEX, lc->ref);
		lc->ref = LUA_NOREF;
		lua_cmd_push_buffer(L, lc->out);
		lua_cmd_push_buffer(L, lc->err);
		termo_lua_call(L, 2, 0, TERMO_LUA_BUDGET_MS, item);
	}
	if (lc->orphan)
		lua_cmd_free(lc);
	return (CMD_RETURN_NORMAL);
}

/*
 * Queue a command list with captured output and a callback item after it.
 * After the running command if there is one, else on the global queue.
 */
static struct lua_cmd *
lua_cmd_queue(struct cmd_list *cmdlist, int ref)
{
	struct cmdq_item	*item = termo_lua_item(), *first, *cb, *start;
	struct client		*c = api_held_client();
	struct cmd_find_state	 fs;
	struct lua_cmd		*lc;

	lc = xcalloc(1, sizeof *lc);
	lc->ref = ref;
	lc->out = evbuffer_new();
	lc->err = evbuffer_new();
	if (lc->out == nullptr || lc->err == nullptr)
		fatalx("out of memory");
	if (item != nullptr)
		lc->state = cmdq_copy_state(cmdq_get_state(item), nullptr);
	else if (c != nullptr && cmd_find_from_client(&fs, c, 0) == 0)
		lc->state = cmdq_new_state(&fs, nullptr, 0);
	else
		lc->state = cmdq_new_state(nullptr, nullptr, 0);
	cmdq_capture(lc->state, lc->out, lc->err);

	start = cmdq_get_callback(lua_cmd_start, lc);
	first = cmdq_get_command(cmdlist, lc->state);
	cb = cmdq_get_callback(lua_cmd_done, lc);
	if (item != nullptr) {
		lua_cmd_insert(item, start);
		lua_cmd_insert(item, first);
		lua_cmd_insert(item, cb);
	} else {
		cmdq_append(c, start);
		cmdq_append(c, first);
		cmdq_append(c, cb);
	}
	return (lc);
}

static int
api_cmd(lua_State *L)
{
	struct cmd_list		*cmdlist = api_parse(L, luaL_checkstring(L, 1));
	struct cmdq_item	*item = termo_lua_item(), *new_item;
	struct cmdq_state	*state;
	struct client		*c;
	struct cmd_find_state	 fs;
	struct lua_cmd		*lc;

	if (item != nullptr) {
		new_item = cmdq_get_command(cmdlist, cmdq_get_state(item));
		lua_cmd_insert(item, new_item);
		cmd_list_free(cmdlist);
		return (0);
	}
	if (api_held() || cmdq_running(nullptr) != nullptr) {
		c = api_held_client();
		if (c != nullptr && cmd_find_from_client(&fs, c, 0) == 0)
			state = cmdq_new_state(&fs, nullptr, 0);
		else
			state = cmdq_new_state(nullptr, nullptr, 0);
		new_item = cmdq_get_command(cmdlist, state);
		cmdq_free_state(state);
		cmdq_append(c, new_item);
		cmd_list_free(cmdlist);
		return (0);
	}

	lc = lua_cmd_queue(cmdlist, LUA_NOREF);
	cmd_list_free(cmdlist);
	cmdq_next(nullptr);
	if (!lc->done) {
		lua_pushnil(L);
		if (lc->started)
			lua_pushstring(L, "asynchronous command, use cmd_async");
		else
			lua_pushstring(L, "command queue busy, use cmd_async");
		lc->orphan = true;
		return (2);
	}
	lua_cmd_push_buffer(L, lc->out);
	lua_cmd_push_buffer(L, lc->err);
	lua_cmd_free(lc);
	return (2);
}

static int
api_cmd_async(lua_State *L)
{
	struct cmd_list	*cmdlist;
	struct lua_cmd	*lc;

	luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	cmdlist = api_parse(L, lua_tostring(L, 1));
	lua_pushvalue(L, 2);
	lc = lua_cmd_queue(cmdlist, luaL_ref(L, LUA_REGISTRYINDEX));
	lc->orphan = true;
	cmd_list_free(cmdlist);
	return (0);
}

const struct api_fn termo_api_cmd[] = {
	{ "cmd", api_cmd, "cmd(string) -> output, error",
	  "run commands; inside a command, event or UI callback they are "
	  "queued and nothing is returned, from a timer they run now" },
	{ "cmd_async", api_cmd_async, "cmd_async(string, fn(output, error))",
	  "run commands and call fn with their captured output when done" },
	{ nullptr, nullptr, nullptr, nullptr },
};
