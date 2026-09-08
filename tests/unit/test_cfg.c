#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"
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
	union options_value		*ov;
	struct key_table		*table;
	struct key_binding		*bd;
	char				*s;
	u_int				 n = 0;

	REQUIRE_EQ(load_termo_conf(), 0);

	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 50000);
	CHECK_EQ(options_get_number(global_s_options, "renumber-windows"), 1);
	CHECK_EQ(options_get_number(global_s_options, "mouse"), 1);
	CHECK_EQ(options_get_number(global_options, "escape-time"), 10);
	CHECK_EQ(options_get_number(global_options, "focus-events"), 1);
	CHECK_EQ(options_get_number(global_options, "set-clipboard"), 2);
	CHECK_EQ(options_get_number(global_w_options, "mode-keys"), MODEKEY_VI);
	CHECK_EQ(options_get_number(global_s_options, "status-keys"), MODEKEY_VI);
	CHECK_EQ(options_get_string(global_options, "default-terminal"), "tmux-256color");

	e = options_get(global_options, "terminal-features");
	REQUIRE_NONNULL(e);
	for (item = options_array_first(e); item != nullptr;
	    item = options_array_next(item))
		n++;
	CHECK_EQ(n, 4u);
	ov = options_array_get(e, "3");
	REQUIRE_NONNULL(ov);
	CHECK_EQ(ov->string, "*:256:RGB");
	ov = options_array_get(e, "0");
	REQUIRE_NONNULL(ov);
	CHECK_EQ(strncmp(ov->string, "xterm*", 6), 0);

	table = key_bindings_get_table("copy-mode-vi", 0);
	REQUIRE_NONNULL(table);
	bd = key_bindings_get(table, 'v');
	REQUIRE_NONNULL(bd);
	s = cmd_list_print(bd->cmdlist, 0);
	CHECK_EQ(s, "send-keys -X begin-selection");
	free(s);
	bd = key_bindings_get(table, 'y');
	REQUIRE_NONNULL(bd);
	s = cmd_list_print(bd->cmdlist, 0);
	CHECK_EQ(s, "send-keys -X copy-pipe-and-cancel");
	free(s);
}

TEST(cfg, source_file_nests_and_reports_missing)
{
	u_int	before = cfg_ncauses;

	CHECK_EQ(load("source-file " TERMO_SOURCE_ROOT "/etc/termo.conf\n"), 0);
	/*
	 * file_read completes from an event_once callback, and the sourced
	 * commands only run on the queue pass after it.
	 */
	termo_test_drain();
	termo_test_drain();
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 50000);
	CHECK_EQ(cfg_ncauses, before);

	CHECK_EQ(load("source-file /nonexistent.conf\n"), 0);
	CHECK_EQ(cfg_ncauses, before + 1);

	CHECK_EQ(load("source-file -q /nonexistent.conf\n"), 0);
	CHECK_EQ(cfg_ncauses, before + 1);
}

TEST(cfg, load_cfg_missing_file_quiet_only_with_flag)
{
	u_int	before = cfg_ncauses;

	CHECK_EQ(load_cfg("/nonexistent/termo.conf", nullptr, nullptr, nullptr,
	    0, nullptr), -1);
	CHECK_EQ(cfg_ncauses, before + 1);
	CHECK_EQ(load_cfg("/nonexistent/termo.conf", nullptr, nullptr, nullptr,
	    CMD_PARSE_QUIET, nullptr), 0);
	CHECK_EQ(cfg_ncauses, before + 1);

	CHECK_EQ(load_cfg(TERMO_SOURCE_ROOT "/etc/termo.conf", nullptr, nullptr,
	    nullptr, CMD_PARSE_PARSEONLY, nullptr), 0);
	termo_test_drain();
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 2000);
	CHECK_EQ(cfg_ncauses, before + 1);
}

TEST(cfg, add_cause_and_print_causes_drain)
{
	struct cmdq_item	*item;
	u_int			 before = cfg_ncauses;

	cfg_add_cause("x %d", 1);
	cfg_add_cause("y");
	CHECK_EQ(cfg_ncauses, before + 2);

	cfg_show_causes(nullptr);
	CHECK_EQ(cfg_ncauses, before + 2);

	item = termo_test_item();
	cfg_print_causes(item);
	CHECK_EQ(cfg_ncauses, 0u);
	termo_test_item_free(item);
}

TEST(cfg, buffer_honours_if_and_environment_lines)
{
	struct environ_entry	*envent;

	CHECK_EQ(load("%if #{==:1,1}\n"
	    "set -g base-index 3\n"
	    "%else\n"
	    "set -g base-index 4\n"
	    "%endif\n"
	    "TERMO_CFG_VAR=hello\n"
	    "%hidden TERMO_CFG_HID=1\n"), 0);
	CHECK_EQ(options_get_number(global_s_options, "base-index"), 3);

	envent = environ_find(global_environ, "TERMO_CFG_VAR");
	REQUIRE_NONNULL(envent);
	CHECK_EQ(envent->value, "hello");
	CHECK_EQ(envent->flags & ENVIRON_HIDDEN, 0);
	envent = environ_find(global_environ, "TERMO_CFG_HID");
	REQUIRE_NONNULL(envent);
	CHECK_EQ(envent->value, "1");
	CHECK(envent->flags & ENVIRON_HIDDEN);
}
