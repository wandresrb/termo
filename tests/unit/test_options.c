#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

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
