#ifndef TERMO_LUA_RUNTIME_H
#define TERMO_LUA_RUNTIME_H

#include <lua.h>

struct cmdq_item;

/* One state, owned by the server. */
void		 termo_lua_init(void);
void		 termo_lua_free(void);
lua_State	*termo_lua_state(void);

/* The command running the current Lua call, or nullptr. */
struct cmdq_item *termo_lua_item(void);

/*
 * The only way C enters Lua: the function and its arguments are on the stack,
 * the call gets budget_ms of wall time, errors are reported and cleared.
 * Returns 0 or -1.
 */
int		 termo_lua_call(lua_State *, int, int, u_int, struct cmdq_item *);
int		 termo_lua_load_file(const char *, struct cmdq_item *);
int		 termo_lua_eval(const char *, struct cmdq_item *, char **);

/* api.c */
void		 termo_lua_api_register(lua_State *);

#endif
