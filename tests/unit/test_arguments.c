#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"
#include "test.h"

/* Real templates; the entries are only declared in src/cmd/cmd.c. */
extern const struct cmd_entry cmd_new_window_entry;
extern const struct cmd_entry cmd_set_option_entry;
extern const struct cmd_entry cmd_resize_pane_entry;
extern const struct cmd_entry cmd_if_shell_entry;

/* Parse argv against a real command's template; cause is set on failure. */
static struct args *
parse(const struct cmd_entry *entry, char **cause, ...)
{
	struct args_value	*values;
	struct args		*args;
	char			*argv[16];
	int			 argc = 0;
	va_list			 ap;
	const char		*s;

	/* values[0] is the command name, as cmd_parse passes it. */
	argv[argc++] = (char *)entry->name;
	va_start(ap, cause);
	while ((s = va_arg(ap, const char *)) != nullptr && argc < 16)
		argv[argc++] = (char *)s;
	va_end(ap);

	values = args_from_vector(argc, argv);
	*cause = nullptr;
	args = args_parse(&entry->args, values, argc, cause);
	args_free_values(values, argc);
	free(values);
	return (args);
}

static void
value_string(struct args_value *v, const char *s)
{
	memset(v, 0, sizeof *v);
	v->type = ARGS_STRING;
	v->string = xstrdup(s);
}

static void
value_commands(struct args_value *v, struct cmd_list *cmdlist)
{
	memset(v, 0, sizeof *v);
	v->type = ARGS_COMMANDS;
	v->cmdlist = cmdlist;
	cmdlist->references++;
}

static struct cmd_list *
parse_list(const char *s)
{
	struct cmd_parse_result	*pr = cmd_parse_from_string(s, nullptr);

	if (pr->status != CMD_PARSE_SUCCESS) {
		test_fail(__FILE__, __LINE__, "parse(%s): %s", s, pr->error);
		free(pr->error);
		return (nullptr);
	}
	return (pr->cmdlist);
}

TEST(arguments, flags_values_and_positionals)
{
	char		*cause;
	struct args	*args = parse(&cmd_new_window_entry, &cause, "-d", "-n",
	    "name", "-c", "/tmp", "ls -la", nullptr);

	REQUIRE_NONNULL(args);
	CHECK(args_has(args, 'd'));
	CHECK(!args_has(args, 'k'));
	CHECK_EQ(args_get(args, 'n'), "name");
	CHECK_EQ(args_get(args, 'c'), "/tmp");
	CHECK_NULL(args_get(args, 'd'));
	CHECK_NULL(args_get(args, 't'));
	CHECK_EQ(args_count(args), 1u);
	CHECK_EQ(args_string(args, 0), "ls -la");
	args_free(args);
}

TEST(arguments, combined_flags_and_repeats)
{
	char			*cause;
	struct args		*args = parse(&cmd_new_window_entry, &cause, "-dk",
	    "-c", "one", "-c", "two", nullptr);
	struct args_value	*v;

	REQUIRE_NONNULL(args);
	CHECK(args_has(args, 'd'));
	CHECK(args_has(args, 'k'));
	CHECK_EQ(args_get(args, 'c'), "two");
	v = args_first_value(args, 'c');
	REQUIRE_NONNULL(v);
	CHECK_EQ(v->string, "one");
	v = args_next_value(v);
	REQUIRE_NONNULL(v);
	CHECK_EQ(v->string, "two");
	CHECK_NULL(args_next_value(v));
	args_free(args);
}

TEST(arguments, double_dash_ends_flags)
{
	char		*cause;
	struct args	*args = parse(&cmd_new_window_entry, &cause, "-d", "--",
	    "-n", "still-positional", nullptr);

	REQUIRE_NONNULL(args);
	CHECK(args_has(args, 'd'));
	CHECK(!args_has(args, 'n'));
	CHECK_EQ(args_count(args), 2u);
	CHECK_EQ(args_string(args, 0), "-n");
	args_free(args);
}

TEST(arguments, errors_name_the_flag)
{
	char		*cause;
	struct args	*args;

	args = parse(&cmd_new_window_entry, &cause, "-Q", nullptr);
	CHECK_NULL(args);
	REQUIRE_NONNULL(cause);
	CHECK_EQ(cause, "unknown flag -Q");
	free(cause);

	args = parse(&cmd_new_window_entry, &cause, "-n", nullptr);
	CHECK_NULL(args);
	REQUIRE_NONNULL(cause);
	CHECK_EQ(cause, "-n expects an argument");
	free(cause);
}

TEST(arguments, positional_bounds)
{
	char		*cause;
	struct args	*args;

	args = parse(&cmd_set_option_entry, &cause, "-g", nullptr);
	CHECK_NULL(args);
	REQUIRE_NONNULL(cause);
	CHECK_NONNULL(strstr(cause, "too few arguments"));
	free(cause);

	args = parse(&cmd_set_option_entry, &cause, "a", "b", "c", nullptr);
	CHECK_NULL(args);
	REQUIRE_NONNULL(cause);
	CHECK_NONNULL(strstr(cause, "too many arguments"));
	free(cause);

	args = parse(&cmd_set_option_entry, &cause, "-g", "status", "off", nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_count(args), 2u);
	args_free(args);
}

TEST(arguments, strtonum_and_percentage)
{
	char		*cause;
	struct args	*args = parse(&cmd_new_window_entry, &cause, "-n", "12",
	    nullptr);
	long long	 n;

	REQUIRE_NONNULL(args);
	n = args_strtonum(args, 'n', 0, 100, &cause);
	CHECK_EQ(n, 12);
	CHECK_NULL(cause);
	n = args_strtonum(args, 'n', 0, 10, &cause);
	CHECK_EQ(n, 0);
	REQUIRE_NONNULL(cause);
	free(cause);
	cause = nullptr;
	n = args_strtonum(args, 'x', 0, 10, &cause);
	CHECK_EQ(n, 0);
	REQUIRE_NONNULL(cause);
	free(cause);
	args_free(args);

	cause = nullptr;
	CHECK_EQ(args_string_percentage("50%", 0, 200, 80, &cause), 40);
	CHECK_NULL(cause);
	CHECK_EQ(args_string_percentage("30", 0, 200, 80, &cause), 30);
	CHECK_EQ(args_string_percentage("abc", 0, 200, 80, &cause), 0);
	REQUIRE_NONNULL(cause);
	free(cause);
}

TEST(arguments, print_reproduces_command_line)
{
	char		*cause, *s;
	struct args	*args = parse(&cmd_new_window_entry, &cause, "-d", "-n",
	    "my win", "echo", nullptr);

	REQUIRE_NONNULL(args);
	s = args_print(args);
	CHECK_EQ(s, "-d -n \"my win\" echo");
	free(s);
	args_free(args);
}

TEST(arguments, escape_quotes_what_the_parser_needs)
{
	static const struct {
		const char	*in, *out;
	} cases[] = {
		{ "plain", "plain" },
		{ "", "''" },
		{ "a b", "\"a b\"" },
		{ "a;b", "\"a;b\"" },
		{ "a\"b", "'a\"b'" },
		{ "#", "\\#" },
		{ "~x", "\\~x" },
		{ "a\tb", "a\\tb" },
		{ "$HOME", "\"\\$HOME\"" },
	};
	u_int	 i;
	char	*s;

	for (i = 0; i < nitems(cases); i++) {
		s = args_escape(cases[i].in);
		if (strcmp(s, cases[i].out) != 0) {
			test_fail(__FILE__, __LINE__, "escape(%s): got %s, want %s",
			    cases[i].in, s, cases[i].out);
		}
		free(s);
	}
	CHECK(1);
}

TEST(arguments, first_next_iterate_flags_in_order)
{
	char			*cause;
	struct args		*args = parse(&cmd_new_window_entry, &cause,
	    "-d", "-n", "x", "-c", "/tmp", "-d", nullptr);
	struct args_entry	*entry;

	REQUIRE_NONNULL(args);
	CHECK_EQ(args_first(args, &entry), 'c');
	CHECK_EQ(args_next(&entry), 'd');
	CHECK_EQ(args_next(&entry), 'n');
	CHECK_EQ(args_next(&entry), 0);
	CHECK_NULL(entry);
	CHECK_EQ(args_has(args, 'd'), 2);
	args_free(args);
}

TEST(arguments, set_and_create_build_args_by_hand)
{
	struct args		*a = args_create();
	struct args_value	*v;
	char			*s;

	CHECK_EQ(args_count(a), 0u);
	args_set(a, 'x', nullptr, 0);
	args_set(a, 'x', nullptr, 0);
	CHECK_EQ(args_has(a, 'x'), 2);
	CHECK_NULL(args_get(a, 'x'));

	v = xcalloc(1, sizeof *v);
	value_string(v, "val");
	args_set(a, 'y', v, 0);
	CHECK_EQ(args_has(a, 'y'), 1);
	CHECK_EQ(args_get(a, 'y'), "val");

	v = xcalloc(1, sizeof *v);
	v->type = ARGS_NONE;
	args_set(a, 'z', v, 0);
	CHECK_EQ(args_has(a, 'z'), 1);
	CHECK_NULL(args_get(a, 'z'));

	s = args_print(a);
	CHECK_EQ(s, "-xxz -y val");
	free(s);
	args_free(a);
}

TEST(arguments, values_and_value_index_bounds)
{
	char		*cause;
	struct args	*args = parse(&cmd_new_window_entry, &cause, "-d",
	    "one", "two", nullptr);

	REQUIRE_NONNULL(args);
	REQUIRE_EQ(args_count(args), 2u);
	CHECK_EQ(args_values(args)[0].string, "one");
	CHECK_EQ(args_value(args, 1)->string, "two");
	CHECK_NULL(args_value(args, 2));
	CHECK_NULL(args_string(args, 2));
	args_free(args);
}

TEST(arguments, to_vector_and_from_vector_round_trip)
{
	char			*cause, **argv;
	struct args		*args = parse(&cmd_new_window_entry, &cause,
	    "--", "ls", "-la", "x y", nullptr);
	struct args_value	*values;
	int			 argc;

	REQUIRE_NONNULL(args);
	args_to_vector(args, &argc, &argv);
	REQUIRE_EQ(argc, 3);
	CHECK_EQ(argv[0], "ls");
	CHECK_EQ(argv[1], "-la");
	CHECK_EQ(argv[2], "x y");

	values = args_from_vector(argc, argv);
	CHECK_EQ(values[0].type, ARGS_STRING);
	CHECK_EQ(values[0].string, "ls");
	CHECK_EQ(values[1].string, "-la");
	CHECK_EQ(values[2].string, "x y");

	args_free_values(values, argc);
	free(values);
	cmd_free_argv(argc, argv);
	args_free(args);
}

TEST(arguments, copy_expands_templates)
{
	char		*cause;
	struct args	*args = parse(&cmd_new_window_entry, &cause, "-n",
	    "win-%1", "-d", "echo %1 %2", nullptr);
	struct args	*c;

	REQUIRE_NONNULL(args);
	c = args_copy(args, 2, (char *[]){ "A", "B" });
	CHECK_EQ(args_get(c, 'n'), "win-A");
	CHECK(args_has(c, 'd'));
	CHECK_EQ(args_string(c, 0), "echo A B");
	CHECK_EQ(args_get(args, 'n'), "win-%1");
	CHECK_EQ(args_string(args, 0), "echo %1 %2");
	args_free(c);

	c = args_copy(args, 0, nullptr);
	CHECK_EQ(args_get(c, 'n'), "win-%1");
	CHECK(args_has(c, 'd'));
	CHECK_EQ(args_string(c, 0), "echo %1 %2");
	args_free(c);
	args_free(args);
}

TEST(arguments, percentage_flag_variants)
{
	char		*cause;
	struct args	*args = parse(&cmd_new_window_entry, &cause, "-d",
	    "-c", "50%", nullptr);

	REQUIRE_NONNULL(args);
	CHECK_EQ(args_percentage(args, 'c', 0, 200, 80, &cause), 40);
	CHECK_NULL(cause);
	CHECK_EQ(args_percentage(args, 'd', 0, 200, 80, &cause), 0);
	CHECK_EQ(cause, "empty");
	free(cause);
	CHECK_EQ(args_percentage(args, 'z', 0, 200, 80, &cause), 0);
	CHECK_EQ(cause, "missing");
	free(cause);
	args_free(args);

	args = parse(&cmd_new_window_entry, &cause, "-c", "150%", nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_percentage(args, 'c', 0, 100, 80, &cause), 0);
	CHECK_EQ(cause, "too large");
	free(cause);
	args_free(args);

	args = parse(&cmd_new_window_entry, &cause, "-c", "10%", nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_percentage(args, 'c', 20, 100, 80, &cause), 0);
	CHECK_EQ(cause, "too small");
	free(cause);
	args_free(args);

	args = parse(&cmd_new_window_entry, &cause, "-c", "abc%", nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_percentage(args, 'c', 0, 100, 80, &cause), 0);
	CHECK_EQ(cause, "invalid");
	free(cause);
	args_free(args);

	CHECK_EQ(args_string_percentage("", 0, 100, 80, &cause), 0);
	CHECK_EQ(cause, "empty");
	free(cause);
}

TEST(arguments, strtonum_and_percentage_expand_formats)
{
	struct cmdq_item	*item = termo_test_item();
	char			*cause;
	struct args		*args;

	args = parse(&cmd_new_window_entry, &cause, "-n", "#{e|+|:1,2}",
	    nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_strtonum_and_expand(args, 'n', 0, 10, item, &cause), 3);
	CHECK_NULL(cause);
	CHECK_EQ(args_strtonum_and_expand(args, 'z', 0, 10, item, &cause), 0);
	CHECK_EQ(cause, "missing");
	free(cause);
	args_free(args);

	args = parse(&cmd_new_window_entry, &cause, "-c", "#{e|*|:5,10}%",
	    nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_percentage_and_expand(args, 'c', 0, 200, 80, item,
	    &cause), 40);
	CHECK_NULL(cause);
	CHECK_EQ(args_percentage_and_expand(args, 'z', 0, 200, 80, item,
	    &cause), 0);
	CHECK_EQ(cause, "missing");
	free(cause);
	args_free(args);

	CHECK_EQ(args_string_percentage_and_expand("#{e|+|:20,10}", 0, 100, 50,
	    item, &cause), 30);
	CHECK_NULL(cause);
	CHECK_EQ(args_string_percentage_and_expand("", 0, 100, 50, item,
	    &cause), 0);
	CHECK_EQ(cause, "empty");
	free(cause);

	termo_test_item_free(item);
}

TEST(arguments, make_commands_from_string_block_and_default)
{
	struct cmdq_item		*item = termo_test_item();
	struct cmd_list			*cmdlist, *cl, *block;
	struct cmd			*self;
	struct args_command_state	*st;
	char				*s, *err = nullptr;

	cmdlist = parse_list("display-message -p 'display-message -p %1'");
	REQUIRE_NONNULL(cmdlist);
	self = cmd_list_first(cmdlist);
	st = args_make_commands_prepare(self, item, 0, nullptr, 0, 0);
	s = args_make_commands_get_command(st);
	CHECK_EQ(s, "display-message");
	free(s);
	cl = args_make_commands(st, 1, (char *[]){ "hi" }, &err);
	REQUIRE_NONNULL(cl);
	s = cmd_list_print(cl, 0);
	CHECK_EQ(s, "display-message -p hi");
	free(s);
	cmd_list_free(cl);
	args_make_commands_free(st);

	cl = args_make_commands_now(self, item, 0, 0);
	REQUIRE_NONNULL(cl);
	s = cmd_list_print(cl, 0);
	CHECK_EQ(s, "display-message -p \"%1\"");
	free(s);
	cmd_list_free(cl);
	cmd_list_free(cmdlist);

	cmdlist = parse_list("display-message -p 'nosuchcmd %1'");
	REQUIRE_NONNULL(cmdlist);
	self = cmd_list_first(cmdlist);
	st = args_make_commands_prepare(self, item, 0, nullptr, 0, 0);
	cl = args_make_commands(st, 1, (char *[]){ "hi" }, &err);
	CHECK_NULL(cl);
	CHECK_EQ(err, "unknown command: nosuchcmd");
	free(err);
	args_make_commands_free(st);
	cmd_list_free(cmdlist);

	cmdlist = parse_list("if-shell -F 1 { display-message -p yes }");
	REQUIRE_NONNULL(cmdlist);
	self = cmd_list_first(cmdlist);
	REQUIRE_EQ(args_count(cmd_get_args(self)), 2u);
	CHECK_EQ(args_value(cmd_get_args(self), 1)->type, ARGS_COMMANDS);
	block = args_value(cmd_get_args(self), 1)->cmdlist;
	st = args_make_commands_prepare(self, item, 1, nullptr, 0, 0);
	s = args_make_commands_get_command(st);
	CHECK_EQ(s, "display-message");
	free(s);
	cl = args_make_commands(st, 0, nullptr, &err);
	CHECK(cl == block);
	cmd_list_free(cl);
	cl = args_make_commands(st, 1, (char *[]){ "hi" }, &err);
	REQUIRE_NONNULL(cl);
	CHECK(cl != block);
	s = cmd_list_print(cl, 0);
	CHECK_EQ(s, "display-message -p yes");
	free(s);
	cmd_list_free(cl);
	args_make_commands_free(st);

	st = args_make_commands_prepare(self, item, 5,
	    "display-message -p dflt", 0, 0);
	s = args_make_commands_get_command(st);
	CHECK_EQ(s, "display-message");
	free(s);
	cl = args_make_commands(st, 0, nullptr, &err);
	REQUIRE_NONNULL(cl);
	s = cmd_list_print(cl, 0);
	CHECK_EQ(s, "display-message -p dflt");
	free(s);
	cmd_list_free(cl);
	args_make_commands_free(st);
	cmd_list_free(cmdlist);

	termo_test_item_free(item);
}

TEST(arguments, parse_enforces_value_types)
{
	struct cmd_list		*cmdlist;
	struct args_value	 values[3];
	struct args		*args;
	char			*cause = nullptr;

	cmdlist = parse_list("display-message -p x");
	REQUIRE_NONNULL(cmdlist);

	value_string(&values[0], "new-window");
	value_string(&values[1], "-n");
	value_commands(&values[2], cmdlist);
	args = args_parse(&cmd_new_window_entry.args, values, 3, &cause);
	CHECK_NULL(args);
	CHECK_EQ(cause, "-n argument must be a string");
	free(cause);
	args_free_values(values, 3);

	cause = nullptr;
	value_string(&values[0], "new-window");
	value_commands(&values[1], cmdlist);
	args = args_parse(&cmd_new_window_entry.args, values, 2, &cause);
	CHECK_NULL(args);
	CHECK_EQ(cause, "argument 1 must be \"string\"");
	free(cause);
	args_free_values(values, 2);

	cause = nullptr;
	value_string(&values[0], "if-shell");
	value_string(&values[1], "true");
	value_commands(&values[2], cmdlist);
	args = args_parse(&cmd_if_shell_entry.args, values, 3, &cause);
	REQUIRE_NONNULL(args);
	CHECK_NULL(cause);
	CHECK_EQ(args_count(args), 2u);
	CHECK_EQ(args_string(args, 0), "true");
	CHECK_EQ(args_value(args, 1)->type, ARGS_COMMANDS);
	CHECK(args_value(args, 1)->cmdlist == cmdlist);
	CHECK_EQ(args_string(args, 1), "display-message -p x");
	args_free(args);
	args_free_values(values, 3);

	cmd_list_free(cmdlist);
}

TEST(arguments, optional_flag_arguments)
{
	char		*cause, *s;
	struct args	*args;

	args = parse(&cmd_resize_pane_entry, &cause, "-D", nullptr);
	REQUIRE_NONNULL(args);
	CHECK(args_has(args, 'D'));
	CHECK_NULL(args_get(args, 'D'));
	s = args_print(args);
	CHECK_EQ(s, "-D --");
	free(s);
	args_free(args);

	args = parse(&cmd_resize_pane_entry, &cause, "-D", "5", nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_get(args, 'D'), "5");
	CHECK_EQ(args_count(args), 0u);
	args_free(args);

	args = parse(&cmd_resize_pane_entry, &cause, "-D5", nullptr);
	REQUIRE_NONNULL(args);
	CHECK_EQ(args_get(args, 'D'), "5");
	args_free(args);

	args = parse(&cmd_resize_pane_entry, &cause, "-D", "-L", nullptr);
	REQUIRE_NONNULL(args);
	CHECK(args_has(args, 'D'));
	CHECK(args_has(args, 'L'));
	CHECK_NULL(args_get(args, 'D'));
	CHECK_NULL(args_get(args, 'L'));
	args_free(args);

	args = parse(&cmd_resize_pane_entry, &cause, "-D", "--", "x", nullptr);
	REQUIRE_NONNULL(args);
	CHECK(args_has(args, 'D'));
	CHECK_NULL(args_get(args, 'D'));
	CHECK_EQ(args_count(args), 1u);
	CHECK_EQ(args_string(args, 0), "x");
	s = args_print(args);
	CHECK_EQ(s, "-D -- x");
	free(s);
	args_free(args);
}
