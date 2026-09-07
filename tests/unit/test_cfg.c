#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

static int
load(const char *text)
{
	int	rc;

	rc = load_cfg_from_buffer(text, strlen(text), "test", nullptr, nullptr,
	    nullptr, 0, nullptr);
	while (cmdq_next(nullptr) != 0)
		;
	return (rc);
}

static int
load_termo_conf(void)
{
	int	rc;

	rc = load_cfg(TERMO_SOURCE_ROOT "/etc/termo.conf", nullptr, nullptr,
	    nullptr, 0, nullptr);
	while (cmdq_next(nullptr) != 0)
		;
	return (rc);
}

TEST(cfg, parse_error_is_reported)
{
	u_int	before = cfg_ncauses;

	CHECK_EQ(load("if-shell {\n"), -1);
	CHECK_EQ(cfg_ncauses, before + 1);
}

TEST(cfg, set_changes_the_tree)
{
	u_int	before = cfg_ncauses;

	CHECK_EQ(load("set -g history-limit 12345\n"), 0);
	CHECK_EQ(cfg_ncauses, before);
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 12345);
}

TEST(cfg, unknown_option_is_a_cause)
{
	u_int	before = cfg_ncauses;

	CHECK_EQ(load("set -g no-such-option 1\n"), 0);
	CHECK_EQ(cfg_ncauses, before + 1);
}

TEST(cfg, parseonly_does_not_execute)
{
	long long	old = options_get_number(global_s_options, "base-index");

	CHECK_EQ(load_cfg_from_buffer("set -g base-index 9\n", 20, "t", nullptr,
	    nullptr, nullptr, CMD_PARSE_PARSEONLY, nullptr), 0);
	CHECK_EQ(options_get_number(global_s_options, "base-index"), old);
}

TEST(cfg, termo_conf_loads_clean)
{
	u_int	before = cfg_ncauses;

	CHECK_EQ(load_termo_conf(), 0);
	CHECK_EQ(cfg_ncauses, before);
}

TEST(cfg, termo_conf_sets_every_promised_default)
{
	struct options_entry		*e;
	struct options_array_item	*item;
	struct key_table		*table;
	struct key_binding		*bd;
	char				*s;
	int				 rgb = 0;

	REQUIRE_EQ(load_termo_conf(), 0);

	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 50000);
	CHECK_EQ(options_get_number(global_s_options, "renumber-windows"), 1);
	CHECK_EQ(options_get_number(global_options, "escape-time"), 10);
	CHECK_EQ(options_get_number(global_options, "focus-events"), 1);
	CHECK_EQ(options_get_number(global_options, "set-clipboard"), 2);
	CHECK_EQ(options_get_number(global_w_options, "mode-keys"), MODEKEY_VI);
	CHECK_EQ(options_get_number(global_s_options, "status-keys"), MODEKEY_VI);
	CHECK_EQ(options_get_string(global_options, "default-terminal"), "tmux-256color");

	e = options_get(global_options, "terminal-features");
	for (item = options_array_first(e); item != nullptr;
	    item = options_array_next(item)) {
		if (strcmp(options_array_item_value(item)->string, "*:256:RGB") == 0)
			rgb = 1;
	}
	CHECK(rgb);

	table = key_bindings_get_table("copy-mode-vi", 0);
	REQUIRE_NONNULL(table);
	bd = key_bindings_get(table, 'v');
	REQUIRE_NONNULL(bd);
	s = cmd_list_print(bd->cmdlist, 0);
	CHECK_NONNULL(strstr(s, "begin-selection"));
	free(s);
	bd = key_bindings_get(table, 'y');
	REQUIRE_NONNULL(bd);
	s = cmd_list_print(bd->cmdlist, 0);
	CHECK_NONNULL(strstr(s, "copy-pipe-and-cancel"));
	free(s);
}
