#include <sys/types.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"
#include "test.h"

extern const struct cmd_entry cmd_set_option_entry;

static int
check_str(int line, const char *what, char *got, const char *expect)
{
	int	ok;

	ok = test_eq_str(__FILE__, line, what, got, expect);
	free(got);
	return (ok);
}
#define CHECK_STR(got, expect)	check_str(__LINE__, #got, (got), (expect))

static const char *
item_string(struct options_entry *e, u_int idx)
{
	union options_value	*ov = options_array_getv(e, "%u", idx);

	return (ov == nullptr ? nullptr : ov->string);
}

static int
from_string(struct options *oo, const char *name, const char *value,
    int append, char **cause)
{
	*cause = nullptr;
	return (options_from_string(oo, options_search(name), name, value,
	    append, cause));
}

static int
scope_of(struct cmd_find_state *fs, const char *flags, const char *name,
    struct options **oo, char **cause)
{
	struct args_value	*values;
	struct args		*args;
	char			*argv[3];
	int			 argc = 0, scope;

	argv[argc++] = (char *)cmd_set_option_entry.name;
	if (flags != nullptr)
		argv[argc++] = (char *)flags;
	argv[argc++] = (char *)name;

	values = args_from_vector(argc, argv);
	*cause = nullptr;
	args = args_parse(&cmd_set_option_entry.args, values, argc, cause);
	args_free_values(values, argc);
	free(values);
	if (args == nullptr) {
		test_fail(__FILE__, __LINE__, "args_parse: %s", *cause);
		free(*cause);
		*cause = nullptr;
		return (OPTIONS_TABLE_NONE);
	}

	*oo = nullptr;
	scope = options_scope_from_name(args, 0, name, fs, oo, cause);
	args_free(args);
	return (scope);
}

TEST(options, table_has_unique_names)
{
	const struct options_table_entry	*a, *b;

	for (a = options_table; a->name != nullptr; a++) {
		for (b = a + 1; b->name != nullptr; b++) {
			if (strcmp(a->name, b->name) == 0)
				test_fail(__FILE__, __LINE__, "duplicate %s", a->name);
		}
	}
	CHECK(1);
}

TEST(options, table_entries_are_well_formed)
{
	const struct options_table_entry	*oe;
	u_int					 n;

	for (oe = options_table; oe->name != nullptr; oe++) {
		if (oe->scope == OPTIONS_TABLE_NONE)
			test_fail(__FILE__, __LINE__, "%s: no scope", oe->name);
		if (oe->text == nullptr)
			test_fail(__FILE__, __LINE__, "%s: no text", oe->name);
		switch (oe->type) {
		case OPTIONS_TABLE_NUMBER:
			if (oe->default_num < (long long)oe->minimum ||
			    oe->default_num > (long long)oe->maximum) {
				test_fail(__FILE__, __LINE__,
				    "%s: default %lld outside [%u, %u]", oe->name,
				    oe->default_num, oe->minimum, oe->maximum);
			}
			break;
		case OPTIONS_TABLE_FLAG:
			if (oe->default_num != 0 && oe->default_num != 1) {
				test_fail(__FILE__, __LINE__, "%s: flag default %lld",
				    oe->name, oe->default_num);
			}
			break;
		case OPTIONS_TABLE_CHOICE:
			if (oe->choices == nullptr) {
				test_fail(__FILE__, __LINE__, "%s: no choices", oe->name);
				break;
			}
			for (n = 0; oe->choices[n] != nullptr; n++)
				;
			if (oe->default_num < 0 || (u_int)oe->default_num >= n) {
				test_fail(__FILE__, __LINE__, "%s: default %lld of %u choices",
				    oe->name, oe->default_num, n);
			}
			break;
		case OPTIONS_TABLE_STRING:
			if ((~oe->flags & OPTIONS_TABLE_IS_ARRAY) &&
			    oe->default_str == nullptr) {
				test_fail(__FILE__, __LINE__, "%s: no default string",
				    oe->name);
			}
			break;
		default:
			break;
		}
	}
	CHECK(1);
}

/* Compiled-in defaults are tmux's; termo's live in etc/termo.conf. */
TEST(options, table_defaults_are_upstream)
{
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 2000);
	CHECK_EQ(options_get_number(global_s_options, "renumber-windows"), 0);
	CHECK_EQ(options_get_number(global_options, "focus-events"), 0);
	CHECK_EQ(options_get_number(global_w_options, "mode-keys"), MODEKEY_EMACS);
	CHECK_EQ(options_get_number(global_s_options, "status-keys"), MODEKEY_EMACS);
}

TEST(options, child_falls_back_to_parent)
{
	struct options	*child = options_create(global_s_options);

	CHECK_EQ(options_get_number(child, "history-limit"), 2000);
	options_set_number(child, "history-limit", 10);
	CHECK_EQ(options_get_number(child, "history-limit"), 10);
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 2000);
	CHECK_NONNULL(options_get_only(child, "history-limit"));
	CHECK_NULL(options_get_only(child, "base-index"));
	CHECK_NONNULL(options_get(child, "base-index"));
	options_free(child);
}

TEST(options, set_string_formats_and_appends)
{
	struct options	*o = options_create(global_s_options);

	options_set_string(o, "status-left", 0, "[%s:%d]", "x", 7);
	CHECK_EQ(options_get_string(o, "status-left"), "[x:7]");
	options_set_string(o, "status-left", 1, "+");
	CHECK_EQ(options_get_string(o, "status-left"), "[x:7]+");
	options_free(o);
}

TEST(options, from_string_rejects_bad_values)
{
	struct options				*o = options_create(global_s_options);
	struct options				*w = options_create(global_w_options);
	const struct options_table_entry	*oe;
	char					*cause = nullptr;

	oe = options_search("history-limit");
	CHECK_EQ(options_from_string(o, oe, "history-limit", "abc", 0, &cause), -1);
	CHECK_NONNULL(cause);
	free(cause);
	cause = nullptr;

	oe = options_search("mode-keys");
	CHECK_EQ(options_from_string(w, oe, "mode-keys", "vim", 0, &cause), -1);
	CHECK_NONNULL(cause);
	free(cause);
	cause = nullptr;
	CHECK_EQ(options_from_string(w, oe, "mode-keys", "vi", 0, &cause), 0);
	CHECK_NULL(cause);
	CHECK_EQ(options_get_number(w, "mode-keys"), MODEKEY_VI);

	oe = options_search("mouse");
	CHECK_EQ(options_from_string(o, oe, "mouse", "on", 0, &cause), 0);
	CHECK_EQ(options_get_number(o, "mouse"), 1);
	CHECK_EQ(options_from_string(o, oe, "mouse", "maybe", 0, &cause), -1);
	free(cause);
	options_free(w);
	options_free(o);
}

TEST(options, arrays_index_and_iterate)
{
	struct options			*o = options_create(global_options);
	struct options_entry		*e;
	struct options_array_item	*item;
	char				*cause = nullptr;
	u_int				 n = 0;

	e = options_get(o, "terminal-features");
	CHECK(options_is_array(e));
	CHECK_NONNULL(options_array_first(e));

	e = options_empty(o, options_table_entry(e));
	CHECK_NULL(options_array_first(e));
	CHECK_EQ(options_array_set(e, "0", "*:RGB", 0, &cause), 0);
	CHECK_EQ(options_array_set(e, "3", "xterm*:title", 0, &cause), 0);
	REQUIRE_NONNULL(options_array_get(e, "0"));
	CHECK_EQ(options_array_get(e, "0")->string, "*:RGB");
	CHECK_NULL(options_array_get(e, "1"));
	REQUIRE_NONNULL(options_array_get(e, "3"));
	CHECK_EQ(options_array_get(e, "3")->string, "xterm*:title");
	for (item = options_array_first(e); item != nullptr;
	    item = options_array_next(item))
		n++;
	CHECK_EQ(n, 2u);

	/* Keys may be names; only the empty key and out-of-range numbers fail. */
	CHECK_EQ(options_array_set(e, "xterm", "xterm:RGB", 0, &cause), 0);
	REQUIRE_NONNULL(options_array_get(e, "xterm"));
	CHECK_EQ(options_array_get(e, "xterm")->string, "xterm:RGB");
	CHECK_EQ(options_array_set(e, "007", "padded", 0, &cause), 0);
	CHECK_NONNULL(options_array_get(e, "7"));
	CHECK_EQ(options_array_set(e, "", "bad", 0, &cause), -1);
	CHECK_NONNULL(cause);
	free(cause);
	cause = nullptr;
	CHECK_EQ(options_array_set(e, "99999999999", "bad", 0, &cause), -1);
	CHECK_NONNULL(cause);
	free(cause);
	options_free(o);
}

TEST(options, search_knows_the_table)
{
	CHECK_NONNULL(options_search("history-limit"));
	CHECK_NULL(options_search("no-such-option"));
}

TEST(options, match_expands_unique_prefixes_and_flags_ambiguity)
{
	char	*name, *key = nullptr;
	int	 amb;

	name = options_match("history-l", &key, &amb);
	CHECK_EQ(name, "history-limit");
	CHECK_NULL(key);
	free(name);

	amb = 0;
	CHECK_NULL(options_match("history", &key, &amb));
	CHECK_EQ(amb, 1);
	CHECK_NULL(key);

	amb = 0;
	CHECK_NULL(options_match("status-l", &key, &amb));
	CHECK_EQ(amb, 1);

	name = options_match("renumber", &key, &amb);
	CHECK_EQ(name, "renumber-windows");
	free(name);

	amb = -1;
	name = options_match("@foo[2]", &key, &amb);
	CHECK_EQ(name, "@foo");
	CHECK_EQ(key, "2");
	CHECK_EQ(amb, 0);
	free(name);
	free(key);

	name = options_match("display-panes-color", &key, &amb);
	CHECK_EQ(name, "display-panes-colour");
	CHECK_NULL(key);
	free(name);

	amb = -1;
	CHECK_NULL(options_match("zzz", &key, &amb));
	CHECK_EQ(amb, 0);
	CHECK_NULL(key);

	name = options_match("history-limit[1]", &key, &amb);
	CHECK_EQ(name, "history-limit");
	CHECK_EQ(key, "1");
	free(name);
	free(key);

	amb = -1;
	CHECK_NONNULL(options_match_get(global_s_options, "history-l", &key, 0,
	    &amb));
	CHECK_EQ(amb, 0);
	CHECK_NULL(key);

	amb = -1;
	CHECK_NULL(options_match_get(global_options, "history-l", &key, 0,
	    &amb));
	CHECK_EQ(amb, 0);
	CHECK_NULL(key);

	amb = 0;
	CHECK_NULL(options_match_get(global_s_options, "hist", &key, 0, &amb));
	CHECK_EQ(amb, 1);
	CHECK_NULL(key);
}

TEST(options, parse_splits_name_and_index)
{
	char	 sentinel;
	char	*name, *key = &sentinel;

	name = options_parse("status-format", &key);
	CHECK_EQ(name, "status-format");
	CHECK_NULL(key);
	free(name);

	name = options_parse("status-format[2]", &key);
	CHECK_EQ(name, "status-format");
	CHECK_EQ(key, "2");
	free(name);
	free(key);

	name = options_parse("status-format[02]", &key);
	CHECK_EQ(name, "status-format");
	CHECK_EQ(key, "2");
	free(name);
	free(key);

	name = options_parse("@x[abc]", &key);
	CHECK_EQ(name, "@x");
	CHECK_EQ(key, "abc");
	free(name);
	free(key);

	key = &sentinel;
	CHECK_NULL(options_parse("x[]", &key));
	CHECK_NULL(key);
	key = &sentinel;
	CHECK_NULL(options_parse("x[1]y", &key));
	CHECK_NULL(key);
	key = &sentinel;
	CHECK_NULL(options_parse("x[1", &key));
	CHECK_NULL(key);
	key = &sentinel;
	CHECK_NULL(options_parse("x[99999999999]", &key));
	CHECK_NULL(key);
	key = &sentinel;
	CHECK_NULL(options_parse("", &key));
	CHECK_NULL(key);
}

TEST(options, parse_get_returns_entry_and_key)
{
	struct options		*child;
	struct options_entry	*e;
	char			*key;

	e = options_parse_get(global_s_options, "status-format[1]", &key, 0);
	REQUIRE_NONNULL(e);
	CHECK_EQ(options_table_entry(e)->name, "status-format");
	CHECK_EQ(key, "1");
	free(key);

	CHECK_NULL(options_parse_get(global_s_options, "nope[1]", &key, 0));
	CHECK_NULL(key);

	child = options_create(global_s_options);
	CHECK_NULL(options_parse_get(child, "status-format[1]", &key, 1));
	CHECK_NULL(key);
	e = options_parse_get(child, "status-format[1]", &key, 0);
	CHECK(e == options_get_only(global_s_options, "status-format"));
	CHECK_EQ(key, "1");
	free(key);
	options_free(child);
}

TEST(options, to_string_renders_every_type)
{
	struct options_entry	*e;

	e = options_get(global_s_options, "history-limit");
	CHECK_STR(options_to_string(e, nullptr, 0), "2000");
	e = options_get(global_s_options, "mouse");
	CHECK_STR(options_to_string(e, nullptr, 0), "off");
	CHECK_STR(options_to_string(e, nullptr, 1), "0");
	e = options_get(global_w_options, "mode-keys");
	CHECK_STR(options_to_string(e, nullptr, 0), "emacs");
	e = options_get(global_s_options, "prefix");
	CHECK_STR(options_to_string(e, nullptr, 0), "C-b");
	e = options_get(global_s_options, "status-bg");
	CHECK_STR(options_to_string(e, nullptr, 0), "default");
	e = options_get(global_options, "default-client-command");
	CHECK_STR(options_to_string(e, nullptr, 0), "new-session");

	e = options_get(global_options, "command-alias");
	CHECK_STR(options_to_string(e, nullptr, 0),
	    "split-pane=split-window splitp=split-window "
	    "server-info=show-messages -JT info=show-messages -JT "
	    "choose-window=choose-tree -w choose-session=choose-tree -s");
	CHECK_STR(options_to_string(e, "1", 0), "splitp=split-window");
	CHECK_STR(options_to_string(e, "nope", 0), "");
	CHECK_STR(options_to_string(e, "", 0), "");

	CHECK_STR(options_default_to_string(options_search("prefix")), "C-b");
	CHECK_STR(options_default_to_string(options_search("mouse")), "off");
	CHECK_STR(options_default_to_string(options_search("mode-keys")),
	    "emacs");
	CHECK_STR(options_default_to_string(options_search("history-limit")),
	    "2000");
	CHECK_STR(options_default_to_string(
	    options_search("default-client-command")), "new-session");
	CHECK_STR(options_default_to_string(options_search("status-bg")),
	    "default");
}

TEST(options, from_string_covers_key_colour_command_style_pattern_shell)
{
	struct options	*o = options_create(global_s_options);
	struct options	*w = options_create(global_w_options);
	struct options	*srv = options_create(global_options);
	char		*cause;

	CHECK_EQ(from_string(o, "prefix", "C-a", 0, &cause), 0);
	CHECK_EQ(options_get_number(o, "prefix"), (long long)('a'|KEYC_CTRL));
	CHECK_EQ(from_string(o, "prefix", "C-", 0, &cause), -1);
	CHECK_STR(cause, "bad key: C-");

	CHECK_EQ(from_string(o, "status-bg", "red", 0, &cause), 0);
	CHECK_EQ(options_get_number(o, "status-bg"), 1);
	CHECK_EQ(from_string(o, "status-bg", "notacolour", 0, &cause), -1);
	CHECK_STR(cause, "bad colour: notacolour");

	CHECK_EQ(from_string(srv, "default-client-command", "new-window -d", 0,
	    &cause), 0);
	CHECK_STR(cmd_list_print(options_get_command(srv,
	    "default-client-command"), 0), "new-window -d");
	CHECK_EQ(from_string(srv, "default-client-command", "if-shell {", 0,
	    &cause), -1);
	CHECK_NONNULL(cause);
	free(cause);

	CHECK_EQ(from_string(o, "status-style", "bg=red", 0, &cause), 0);
	CHECK_EQ(from_string(o, "status-style", "bg=zzz", 0, &cause), -1);
	CHECK_STR(cause, "invalid style: bg=zzz");
	CHECK_EQ(options_get_string(o, "status-style"), "bg=red");
	CHECK_EQ(from_string(o, "status-style", "bg=#{x}", 0, &cause), 0);
	CHECK_EQ(options_get_string(o, "status-style"), "bg=#{x}");

	CHECK_EQ(from_string(o, "default-size", "100x50", 0, &cause), 0);
	CHECK_EQ(from_string(o, "default-size", "100", 0, &cause), -1);
	CHECK_STR(cause, "value is invalid: 100");
	CHECK_EQ(options_get_string(o, "default-size"), "100x50");

	CHECK_EQ(from_string(o, "default-shell", "/nonexistent/sh", 0, &cause),
	    -1);
	CHECK_STR(cause, "not a suitable shell: /nonexistent/sh");
	CHECK_EQ(from_string(o, "default-shell", "/bin/sh", 0, &cause), 0);
	CHECK_EQ(options_get_string(o, "default-shell"), "/bin/sh");

	CHECK_EQ(from_string(o, "history-limit", "-1", 0, &cause), -1);
	CHECK_STR(cause, "value is too small: -1");
	CHECK_EQ(from_string(o, "history-limit", nullptr, 0, &cause), -1);
	CHECK_STR(cause, "empty value");

	options_set_number(o, "mouse", 0);
	CHECK_EQ(from_string(o, "mouse", nullptr, 0, &cause), 0);
	CHECK_EQ(options_get_number(o, "mouse"), 1);
	CHECK_EQ(from_string(o, "mouse", nullptr, 0, &cause), 0);
	CHECK_EQ(options_get_number(o, "mouse"), 0);

	CHECK_EQ(options_get_number(w, "mode-keys"), MODEKEY_EMACS);
	CHECK_EQ(from_string(w, "mode-keys", nullptr, 0, &cause), 0);
	CHECK_EQ(options_get_number(w, "mode-keys"), MODEKEY_VI);

	CHECK_EQ(from_string(o, "bogus", "v", 0, &cause), -1);
	CHECK_STR(cause, "bad option name");

	options_set_string(o, "@u", 0, "a");
	CHECK_EQ(from_string(o, "@u", "b", 1, &cause), 0);
	CHECK_EQ(options_get_string(o, "@u"), "ab");

	options_free(srv);
	options_free(w);
	options_free(o);
}

TEST(options, find_choice_maps_names)
{
	const struct options_table_entry	*oe;
	char					*cause = nullptr;

	oe = options_search("mode-keys");
	CHECK_EQ(options_find_choice(oe, "vi", &cause), 1);
	CHECK_EQ(options_find_choice(oe, "emacs", &cause), 0);
	CHECK_EQ(options_find_choice(oe, "zzz", &cause), -1);
	CHECK_STR(cause, "unknown value: zzz");
	oe = options_search("set-clipboard");
	CHECK_EQ(options_find_choice(oe, "external", &cause), 1);
}

TEST(options, array_assign_clear_getv_and_typed_items)
{
	struct options			*srv, *o, *w;
	struct options_entry		*e, *e2, *e3, *hl;
	struct options_array_item	*a;
	union options_value		*ov;
	char				*cause = nullptr;
	u_int				 n = 0;

	srv = options_create(global_options);
	o = options_create(global_s_options);
	w = options_create(global_w_options);

	e = options_empty(srv, options_search("terminal-features"));
	CHECK_EQ(options_array_assign(e, "a:b,c:d", &cause), 0);
	CHECK_EQ(item_string(e, 0), "a:b");
	CHECK_EQ(item_string(e, 1), "c:d");
	CHECK_EQ(options_array_assign(e, "e", &cause), 0);
	CHECK_EQ(item_string(e, 2), "e");
	CHECK_EQ(options_array_assign(e, "", &cause), 0);
	CHECK_EQ(options_array_assign(e, ",,", &cause), 0);
	for (a = options_array_first(e); a != nullptr;
	    a = options_array_next(a))
		n++;
	CHECK_EQ(n, 3u);

	e2 = options_empty(o, options_search("after-new-window"));
	CHECK_EQ(options_array_assign(e2, "display -p a ; display -p b",
	    &cause), 0);
	a = options_array_first(e2);
	REQUIRE_NONNULL(a);
	CHECK_EQ(options_array_item_key(a), "0");
	CHECK_NULL(options_array_next(a));
	CHECK_STR(options_to_string(e2, "0", 0),
	    "display-message -p a ; display-message -p b");
	CHECK_EQ(options_array_set(e2, "1", "if-shell {", 0, &cause), -1);
	CHECK_NONNULL(cause);
	free(cause);
	CHECK_EQ(options_array_set(e2, "0", nullptr, 0, &cause), 0);
	CHECK_NULL(options_array_first(e2));

	e3 = options_empty(w, options_search("pane-colours"));
	CHECK_EQ(options_array_set(e3, "0", "red", 0, &cause), 0);
	ov = options_array_get(e3, "0");
	REQUIRE_NONNULL(ov);
	CHECK_EQ(ov->number, 1);
	CHECK_STR(options_to_string(e3, "0", 0), "red");
	CHECK_EQ(options_array_set(e3, "1", "nope", 0, &cause), -1);
	CHECK_STR(cause, "bad colour: nope");
	CHECK_EQ(options_array_set(e3, "1", "nope", 0, nullptr), -1);
	CHECK_NULL(options_array_get(e3, "1"));

	options_array_clear(e);
	CHECK_NULL(options_array_first(e));
	hl = options_get(global_s_options, "history-limit");
	options_array_clear(hl);
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 2000);
	CHECK_EQ(options_array_set(hl, "0", "1", 0, &cause), -1);
	CHECK_STR(cause, "not an array");

	options_free(w);
	options_free(o);
	options_free(srv);
}

TEST(options, remove_or_default_resets_globals_removes_children)
{
	struct options		*child = options_create(global_s_options);
	struct options		*srv = options_create(global_options);
	struct options_entry	*e;
	char			*cause = nullptr;

	options_set_number(global_s_options, "history-limit", 5);
	e = options_get_only(global_s_options, "history-limit");
	CHECK_EQ(options_remove_or_default(e, nullptr, &cause), 0);
	CHECK_NONNULL(options_get_only(global_s_options, "history-limit"));
	CHECK_EQ(options_get_number(global_s_options, "history-limit"), 2000);

	options_set_number(child, "history-limit", 7);
	e = options_get_only(child, "history-limit");
	CHECK_EQ(options_remove_or_default(e, nullptr, &cause), 0);
	CHECK_NULL(options_get_only(child, "history-limit"));
	CHECK_EQ(options_get_number(child, "history-limit"), 2000);

	e = options_set_string(global_s_options, "@u", 0, "x");
	CHECK_EQ(options_remove_or_default(e, nullptr, &cause), 0);
	CHECK_NULL(options_get_only(global_s_options, "@u"));

	e = options_default(srv, options_search("command-alias"));
	CHECK_NONNULL(options_array_get(e, "0"));
	CHECK_EQ(options_remove_or_default(e, "0", &cause), 0);
	CHECK_NULL(options_array_get(e, "0"));
	CHECK_NONNULL(options_array_get(e, "1"));
	CHECK_EQ(options_remove_or_default(e, "", &cause), -1);
	CHECK_STR(cause, "bad array key: ");

	options_free(srv);
	options_free(child);
}

TEST(options, scope_from_name_and_flags_pick_the_tree)
{
	struct cmd_find_state	 fs;
	struct window		*w;
	struct window_pane	*wp;
	struct options		*oo;
	char			*cause;

	cmd_find_clear_state(&fs, 0);

	CHECK_EQ(scope_of(&fs, "-g", "history-limit", &oo, &cause),
	    OPTIONS_TABLE_SESSION);
	CHECK(oo == global_s_options);
	CHECK_EQ(scope_of(&fs, nullptr, "history-limit", &oo, &cause),
	    OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "no current session");
	CHECK_EQ(scope_of(&fs, "-tfoo", "history-limit", &oo, &cause),
	    OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "no such session: foo");

	CHECK_EQ(scope_of(&fs, nullptr, "escape-time", &oo, &cause),
	    OPTIONS_TABLE_SERVER);
	CHECK(oo == global_options);
	CHECK_EQ(scope_of(&fs, "-gpw", "escape-time", &oo, &cause),
	    OPTIONS_TABLE_SERVER);
	CHECK(oo == global_options);

	CHECK_EQ(scope_of(&fs, "-g", "mode-keys", &oo, &cause),
	    OPTIONS_TABLE_WINDOW);
	CHECK(oo == global_w_options);
	/* mode-keys is window-only: -p is ignored and the window is missing. */
	CHECK_EQ(scope_of(&fs, "-p", "mode-keys", &oo, &cause),
	    OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "no current window");
	CHECK_EQ(scope_of(&fs, "-p", "pane-colours", &oo, &cause),
	    OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "no current pane");
	CHECK_EQ(scope_of(&fs, "-ptfoo", "pane-colours", &oo, &cause),
	    OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "no such pane: foo");

	CHECK_EQ(scope_of(&fs, nullptr, "zzz", &oo, &cause),
	    OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "unknown option: zzz");

	CHECK_EQ(scope_of(&fs, "-s", "@x", &oo, &cause), OPTIONS_TABLE_SERVER);
	CHECK(oo == global_options);
	CHECK_EQ(scope_of(&fs, "-g", "@x", &oo, &cause), OPTIONS_TABLE_SESSION);
	CHECK(oo == global_s_options);
	CHECK_EQ(scope_of(&fs, "-gw", "@x", &oo, &cause), OPTIONS_TABLE_WINDOW);
	CHECK(oo == global_w_options);
	CHECK_EQ(scope_of(&fs, "-w", "@x", &oo, &cause), OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "no current window");
	CHECK_EQ(scope_of(&fs, "-p", "@x", &oo, &cause), OPTIONS_TABLE_NONE);
	CHECK_STR(cause, "no current pane");

	w = termo_test_window(80, 24, 1);
	wp = w->active;
	fs.w = w;
	fs.wp = wp;
	CHECK_EQ(scope_of(&fs, "-p", "@x", &oo, &cause), OPTIONS_TABLE_PANE);
	CHECK(oo == wp->options);
	CHECK_EQ(scope_of(&fs, "-p", "pane-colours", &oo, &cause),
	    OPTIONS_TABLE_PANE);
	CHECK(oo == wp->options);
	termo_test_window_free(w);
}

TEST(options, string_to_style_caches_and_expands)
{
	struct options		*o = options_create(global_s_options);
	struct format_tree	*ft;
	struct style		*sy;

	options_set_string(o, "status-style", 0, "bg=green,fg=black");
	sy = options_string_to_style(o, "status-style", nullptr);
	REQUIRE_NONNULL(sy);
	CHECK_EQ(sy->gc.bg, 2);
	CHECK_EQ(sy->gc.fg, 0);
	CHECK(options_string_to_style(o, "status-style", nullptr) == sy);

	options_set_string(o, "status-style", 0, "bg=#{a}");
	CHECK_NULL(options_string_to_style(o, "status-style", nullptr));
	ft = format_create(nullptr, nullptr, FORMAT_NONE, 0);
	format_add(ft, "a", "%s", "red");
	sy = options_string_to_style(o, "status-style", ft);
	REQUIRE_NONNULL(sy);
	CHECK_EQ(sy->gc.bg, 1);
	format_add(ft, "a", "%s", "blue");
	sy = options_string_to_style(o, "status-style", ft);
	REQUIRE_NONNULL(sy);
	CHECK_EQ(sy->gc.bg, 4);
	format_free(ft);

	CHECK_NULL(options_string_to_style(o, "history-limit", nullptr));
	CHECK_NULL(options_string_to_style(o, "nope", nullptr));
	options_free(o);
}

TEST(options, parent_first_next_name_owner_is_string)
{
	struct options		*c = options_create(nullptr);
	struct options_entry	*e;

	CHECK_NULL(options_get_parent(c));
	options_set_parent(c, global_w_options);
	CHECK(options_get_parent(c) == global_w_options);
	CHECK_NONNULL(options_get(c, "mode-keys"));
	CHECK_NULL(options_get(c, "history-limit"));

	options_set_string(c, "@b", 0, "2");
	options_set_string(c, "@a", 0, "1");
	e = options_first(c);
	REQUIRE_NONNULL(e);
	CHECK_EQ(options_name(e), "@a");
	CHECK(options_owner(e) == c);
	CHECK_EQ(options_is_string(e), 1);
	e = options_next(e);
	REQUIRE_NONNULL(e);
	CHECK_EQ(options_name(e), "@b");
	CHECK_NULL(options_next(e));

	e = options_get(global_s_options, "status-left");
	CHECK_EQ(options_is_string(e), 1);
	e = options_get(global_s_options, "history-limit");
	CHECK_EQ(options_is_string(e), 0);
	options_free(c);
}

TEST(options, set_command_replaces_cmdlist)
{
	struct options		*srv = options_create(global_options);
	struct cmd_parse_result	*pr;

	pr = cmd_parse_from_string("new-window -d", nullptr);
	REQUIRE_EQ(pr->status, CMD_PARSE_SUCCESS);
	/* The entry takes the list's only reference; options_free drops it. */
	CHECK_NONNULL(options_set_command(srv, "default-client-command",
	    pr->cmdlist));
	CHECK_STR(cmd_list_print(options_get_command(srv,
	    "default-client-command"), 0), "new-window -d");
	CHECK_STR(cmd_list_print(options_get_command(global_options,
	    "default-client-command"), 0), "new-session");
	options_free(srv);
}

TEST(options, push_changes_is_safe_and_marks_panes)
{
	struct window		*w = termo_test_window(80, 24, 1);
	struct window_pane	*wp = w->active;
	struct session		*s = termo_test_session("push", w);
	struct options_entry	*e;

	wp->flags &= ~PANE_STYLECHANGED;
	options_push_changes("@x");
	CHECK(wp->flags & PANE_STYLECHANGED);

	options_set_number(global_s_options, "history-limit", 123);
	options_push_changes("history-limit");
	CHECK_EQ(wp->base.grid->hlimit, 123u);

	options_set_number(global_s_options, "status", 0);
	options_push_changes("status");
	CHECK_EQ(s->statuslines, 0u);
	CHECK_EQ(s->statusat, -1);

	e = options_get(global_w_options, "pane-colours");
	CHECK_EQ(options_array_set(e, "0", "red", 0, nullptr), 0);
	options_push_changes("pane-colours");
	if (CHECK_NONNULL(wp->palette.default_palette)) {
		CHECK_EQ(wp->palette.default_palette[0], 1);
		CHECK_EQ(wp->palette.default_palette[1], -1);
	}

	options_push_changes("theme");
	options_push_changes("codepoint-widths");
	options_push_changes("input-buffer-size");
	CHECK(wp->window == w);
	CHECK(w->active == wp);
	CHECK_EQ(wp->base.grid->hlimit, 123u);

	termo_test_session_free(s);
	termo_test_window_free(w);
}

TEST(options, monitor_data_and_hook_counters)
{
	struct options_entry	*e;
	int			 dummy;

	e = options_get(global_s_options, "after-new-window");
	REQUIRE_NONNULL(e);
	CHECK_NULL(options_get_monitor_data(e));
	CHECK_EQ(options_get_fire_count(e), 0u);
	CHECK_EQ(options_get_fire_time(e), 0);

	current_time = 1700000000;
	options_hook_fired(e);
	CHECK_EQ(options_get_fire_count(e), 1u);
	CHECK_EQ(options_get_fire_time(e), 1700000000);
	current_time = 0;
	options_hook_fired(e);
	CHECK_EQ(options_get_fire_count(e), 2u);
	CHECK_EQ(options_get_fire_time(e), 0);

	/* options_remove hands non-NULL data to hooks_monitor_free. */
	options_set_monitor_data(e, &dummy);
	CHECK(options_get_monitor_data(e) == &dummy);
	options_set_monitor_data(e, nullptr);
	CHECK_NULL(options_get_monitor_data(e));
}
