/* Run Lua code in the server: a file with -f, or a chunk. */

#include <sys/types.h>

#include <stdlib.h>

#include "termo.h"
#include "lua/runtime.h"

static enum cmd_retval	cmd_run_lua_exec(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_run_lua_entry = {
	.name = "run-lua",
	.alias = NULL,

	.args = { "f:j", 0, 1, NULL },
	.usage = "[-j] [-f path] [code]",

	.flags = CMD_AFTERHOOK,
	.exec = cmd_run_lua_exec
};

static enum cmd_retval
cmd_run_lua_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args	*args = cmd_get_args(self);
	const char	*path = args_get(args, 'f');
	char		*result;

	if (path != NULL) {
		if (args_count(args) != 0) {
			cmdq_error(item, "-f takes no code argument");
			return (CMD_RETURN_ERROR);
		}
		if (termo_lua_load_file(path, item) != 0)
			return (CMD_RETURN_ERROR);
		return (CMD_RETURN_NORMAL);
	}
	if (args_count(args) != 1) {
		cmdq_error(item, "code or -f path required");
		return (CMD_RETURN_ERROR);
	}
	if (termo_lua_eval(args_string(args, 0), item, &result) != 0)
		return (CMD_RETURN_ERROR);
	if (result != NULL) {
		cmdq_print(item, "%s", result);
		free(result);
	}
	return (CMD_RETURN_NORMAL);
}
