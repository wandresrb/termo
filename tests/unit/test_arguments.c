#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

/* Real templates; the entries are only declared in src/cmd/cmd.c. */
extern const struct cmd_entry cmd_new_window_entry;
extern const struct cmd_entry cmd_set_option_entry;

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
