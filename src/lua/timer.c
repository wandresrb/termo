/*
 * termo.api.defer, timer and system: things that wait. A timer is a
 * userdata holding a libevent timer; while armed the registry keeps it
 * alive, so __gc only runs after stop() or the last fire. A process is a
 * job with its three callbacks bound to Lua functions.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "termo.h"
#include "lua/runtime.h"
#include "lua/api.h"

struct lua_timer {
	struct event	 ev;
	struct timeval	 tv;
	int		 fn;
	int		 self;
	bool		 repeat;
};

struct lua_job {
	int	fns;	/* {on_stdout, on_exit} */
};

static const char TIMER_META[] = "termo.timer";

static void
timer_release(lua_State *L, struct lua_timer *t)
{
	evtimer_del(&t->ev);
	luaL_unref(L, LUA_REGISTRYINDEX, t->fn);
	luaL_unref(L, LUA_REGISTRYINDEX, t->self);
	t->fn = t->self = LUA_NOREF;
}

static void
timer_cb([[maybe_unused]] int fd, [[maybe_unused]] short events, void *arg)
{
	struct lua_timer	*t = arg;
	lua_State		*L = termo_lua_state();

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->fn);
	termo_lua_call(L, 0, 0, TERMO_LUA_BUDGET_MS, nullptr);
	if (t->self == LUA_NOREF)	/* stopped from inside the callback */
		return;
	if (t->repeat)
		evtimer_add(&t->ev, &t->tv);
	else
		timer_release(L, t);
}

static int
timer_stop(lua_State *L)
{
	struct lua_timer	*t = luaL_checkudata(L, 1, TIMER_META);

	if (t->self != LUA_NOREF)
		timer_release(L, t);
	return (0);
}

static int
timer_gc(lua_State *L)
{
	struct lua_timer	*t = luaL_checkudata(L, 1, TIMER_META);

	evtimer_del(&t->ev);
	return (0);
}

static int
timer_new(lua_State *L, bool repeat)
{
	lua_Number		 ms = luaL_checknumber(L, 1);
	struct lua_timer	*t;

	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (ms < 0)
		return (luaL_argerror(L, 1, "negative delay"));

	t = lua_newuserdata(L, sizeof *t);
	memset(t, 0, sizeof *t);
	t->fn = t->self = LUA_NOREF;
	t->repeat = repeat;
	t->tv.tv_sec = (time_t)(ms / 1000);
	t->tv.tv_usec = (suseconds_t)((ms - (double)t->tv.tv_sec * 1000) * 1000);
	evtimer_set(&t->ev, timer_cb, t);

	if (luaL_newmetatable(L, TIMER_META)) {
		lua_pushcfunction(L, timer_gc);
		lua_setfield(L, -2, "__gc");
		lua_newtable(L);
		lua_pushcfunction(L, timer_stop);
		lua_setfield(L, -2, "stop");
		lua_setfield(L, -2, "__index");
	}
	lua_setmetatable(L, -2);

	lua_pushvalue(L, 2);
	t->fn = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushvalue(L, -1);
	t->self = luaL_ref(L, LUA_REGISTRYINDEX);
	evtimer_add(&t->ev, &t->tv);
	return (1);
}

static int
api_defer(lua_State *L)
{
	return (timer_new(L, false));
}

static int
api_timer(lua_State *L)
{
	return (timer_new(L, true));
}

/* Call {on_stdout, on_exit}[field] with one argument already on the stack. */
static void
job_call(lua_State *L, struct lua_job *lj, const char *field)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, lj->fns);
	lua_getfield(L, -1, field);
	lua_remove(L, -2);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 2);
		return;
	}
	lua_insert(L, -2);
	termo_lua_call(L, 1, 0, TERMO_LUA_BUDGET_MS, nullptr);
}

static void
job_lines(struct job *job, bool flush)
{
	struct lua_job		*lj = job_get_data(job);
	lua_State		*L = termo_lua_state();
	struct evbuffer		*evb = job_get_event(job)->input;
	char			*line;
	size_t			 size;

	while ((line = evbuffer_readln(evb, nullptr, EVBUFFER_EOL_LF)) !=
	    nullptr) {
		lua_pushstring(L, line);
		free(line);
		job_call(L, lj, "on_stdout");
	}
	if (!flush || (size = EVBUFFER_LENGTH(evb)) == 0)
		return;
	lua_pushlstring(L, (const char *)EVBUFFER_DATA(evb), size);
	evbuffer_drain(evb, size);
	job_call(L, lj, "on_stdout");
}

static void
lua_job_update(struct job *job)
{
	job_lines(job, false);
}

static void
lua_job_complete(struct job *job)
{
	struct lua_job	*lj = job_get_data(job);
	lua_State	*L = termo_lua_state();
	int		 status = job_get_status(job), code;

	job_lines(job, true);
	if (WIFEXITED(status))
		code = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		code = 128 + WTERMSIG(status);
	else
		code = 0;
	lua_pushinteger(L, code);
	job_call(L, lj, "on_exit");
}

static void
lua_job_free(void *data)
{
	struct lua_job	*lj = data;
	lua_State	*L = termo_lua_state();

	if (L != nullptr)
		luaL_unref(L, LUA_REGISTRYINDEX, lj->fns);
	free(lj);
}

static int
api_system(lua_State *L)
{
	struct cmdq_item	*item = termo_lua_item();
	struct session		*s = nullptr;
	struct lua_job		*lj;
	const char		*cmd = nullptr, *cwd = nullptr;
	char			**argv = nullptr;
	int			 argc = 0, flags = JOB_NOWAIT, i, n;

	if (lua_type(L, 1) == LUA_TSTRING)
		cmd = lua_tostring(L, 1);
	else {
		luaL_checktype(L, 1, LUA_TTABLE);
		n = lua_objlen(L, 1);
		if (n == 0)
			return (luaL_argerror(L, 1, "empty argv"));
		for (i = 1; i <= n; i++) {
			lua_rawgeti(L, 1, i);
			if (lua_type(L, -1) != LUA_TSTRING) {
				cmd_free_argv(argc, argv);
				return (luaL_argerror(L, 1, "argv must be strings"));
			}
			cmd_append_argv(&argc, &argv, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
	if (!lua_isnoneornil(L, 2)) {
		luaL_checktype(L, 2, LUA_TTABLE);
		lua_getfield(L, 2, "cwd");
		cwd = lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 2, "stderr");
		if (lua_toboolean(L, -1))
			flags |= JOB_SHOWSTDERR;
		lua_pop(L, 1);
		lua_pushvalue(L, 2);
	} else
		lua_newtable(L);
	lj = xcalloc(1, sizeof *lj);
	lj->fns = luaL_ref(L, LUA_REGISTRYINDEX);

	if (item != nullptr && cmd_find_valid_state(cmdq_get_target(item)))
		s = cmdq_get_target(item)->s;
	if (job_run(cmd, argc, argv, nullptr, s, cwd, lua_job_update,
	    lua_job_complete, lua_job_free, lj, flags, -1, -1) == nullptr) {
		cmd_free_argv(argc, argv);
		lua_job_free(lj);
		return (luaL_error(L, "failed to run: %s",
		    cmd != nullptr ? cmd : "argv"));
	}
	cmd_free_argv(argc, argv);
	return (0);
}

const struct api_fn termo_api_timer[] = {
	{ "defer", api_defer, "defer(ms, fn) -> timer",
	  "call fn once after ms milliseconds; the timer has stop()" },
	{ "timer", api_timer, "timer(ms, fn) -> timer",
	  "call fn every ms milliseconds until timer:stop()" },
	{ "system", api_system,
	  "system(argv or command, {on_stdout = fn(line), on_exit = fn(status), "
	  "cwd = dir, stderr = bool})",
	  "run a process without blocking; a string runs through the shell" },
	{ nullptr, nullptr, nullptr, nullptr },
};
