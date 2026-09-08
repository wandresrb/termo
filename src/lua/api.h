/* Internal to src/lua: the api function table and helpers shared by its files. */

#ifndef TERMO_LUA_API_H
#define TERMO_LUA_API_H

#include <lua.h>

struct api_fn {
	const char	*name;
	lua_CFunction	 fn;
	const char	*signature;
	const char	*doc;
};

/* Each file exports a nullptr-name terminated table. */
extern const struct api_fn termo_api_core[];
extern const struct api_fn termo_api_cmd[];
extern const struct api_fn termo_api_events[];
extern const struct api_fn termo_api_keymap[];
extern const struct api_fn termo_api_timer[];
extern const struct api_fn termo_api_format[];
extern const struct api_fn termo_api_ui[];

static constexpr u_int TERMO_LUA_BUDGET_MS = 2000;	/* user-driven entry */
static constexpr u_int TERMO_LUA_BUDGET_CB_MS = 50;	/* per event or redraw */

/* api.c */
void	 api_target(lua_State *, int, struct cmd_find_state *);
struct client *api_client(lua_State *, int);
void	 api_push_handle(lua_State *, char, u_int);
int	 api_ref_field(lua_State *, int, const char *);
void	 api_call_ref(lua_State *, int, int, u_int, struct cmdq_item *);
struct cmd_list *api_parse(lua_State *, const char *);

/*
 * While a callback runs on the stack of a core operation (an event sink,
 * a menu, prompt or popup callback), commands may only be queued, on the
 * held client's queue when there is one.
 */
struct client *api_hold(struct client *);
void	 api_release(struct client *);
bool	 api_held(void);
struct client *api_held_client(void);

/* events.c */
void	 termo_lua_events_free(void);

/* keymap.c */
int	 termo_lua_keymap_run(struct cmdq_item *, int);

/* format.c */
void	 termo_lua_format_free(void);
struct format_tree *termo_lua_format_tree(void);

#endif
