#include <sys/types.h>

#include "termo.h"

static enum cmd_retval	cmd_stack_pane_exec(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_stack_pane_entry = {
	.name = "stack-pane",
	.alias = "stackp",

	.args = { "c:e:F:nPs:t:", 0, -1, NULL },
	.usage = "[-nP] [-c start-directory] [-e environment] [-F format] "
		 "[-s src-pane] " CMD_TARGET_PANE_USAGE " "
		 "[shell-command [argument ...]]",

	.source = { 's', CMD_FIND_PANE, 0 },
	.target = { 't', CMD_FIND_PANE, 0 },

	.flags = 0,
	.exec = cmd_stack_pane_exec
};

static enum cmd_retval
cmd_stack_pane_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args		*args = cmd_get_args(self);
	struct cmd_find_state	*current = cmdq_get_current(item);
	struct cmd_find_state	*target = cmdq_get_target(item);
	struct window		*w = target->wl->window;
	struct window_pane	*wp;

	if (args_has(args, 'n')) {
		wp = window_pane_stack_next(target->wp);
		if (wp == w->active)
			return (CMD_RETURN_NORMAL);
		window_redraw_active_switch(w, wp);
		if (window_set_active_pane(w, wp, 1))
			cmd_find_from_winlink_pane(current, target->wl, wp, 0);
		server_redraw_window(w);
		return (CMD_RETURN_NORMAL);
	}
	if (args_has(args, 's'))
		return (cmd_join_pane_exec(self, item));
	return (cmd_split_window_exec(self, item));
}
