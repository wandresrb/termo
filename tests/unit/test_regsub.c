#include <sys/types.h>

#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

static void
expect(const char *file, int line, const char *pattern, const char *with,
    const char *text, int flags, const char *want)
{
	char	*got = regsub(pattern, with, text, flags);

	if (got == NULL) {
		test_fail(file, line, "regsub(%s, %s, %s): got NULL, want \"%s\"",
		    pattern, with, text, want);
		return;
	}
	if (strcmp(got, want) != 0) {
		test_fail(file, line, "regsub(%s, %s, %s): got \"%s\", want \"%s\"",
		    pattern, with, text, got, want);
	}
	free(got);
}
#define EXPECT(p, w, t, want) expect(__FILE__, __LINE__, p, w, t, REG_EXTENDED, want)
#define EXPECT_F(p, w, t, f, want) expect(__FILE__, __LINE__, p, w, t, f, want)

TEST(regsub, replaces_every_match)
{
	EXPECT("l", "L", "hello", "heLLo");
	EXPECT("o", "0", "foo boo", "f00 b00");
	EXPECT("z", "-", "hello", "hello");
}

TEST(regsub, anchored_pattern_replaces_first_only)
{
	EXPECT("^a", "X", "aaa", "Xaa");
	EXPECT("^", "> ", "line", "> line");
}

TEST(regsub, groups_expand_in_replacement)
{
	EXPECT("(h)(e)", "\\2\\1", "hello", "ehllo");
	EXPECT("([a-z]+)@([a-z]+)", "\\2 at \\1", "me@host", "host at me");
	/* Upstream quirk: an unmatched group leaves its digit in the output. */
	EXPECT("(x)?y", "[\\1]", "y", "[1]");
	EXPECT("l", "<\\0>", "hello", "he<l><l>o");
}

TEST(regsub, backslash_escapes)
{
	EXPECT("l", "\\\\", "hello", "he\\\\o");
	EXPECT("l", "a\\", "hello", "hea\\a\\o");
	EXPECT("l", "\\x", "hello", "hexxo");
}

TEST(regsub, icase_flag)
{
	EXPECT_F("L", "_", "hello", REG_EXTENDED | REG_ICASE, "he__o");
	EXPECT_F("L", "_", "hello", REG_EXTENDED, "hello");
}

/* Unlike sed, an empty match at the very start is skipped, not expanded. */
TEST(regsub, empty_matches_advance)
{
	EXPECT("x*", "-", "abc", "a-b-c-");
	EXPECT("b*", "-", "abc", "a-c-");
}

TEST(regsub, edge_inputs)
{
	char	*got;

	EXPECT("a", "b", "", "");
	EXPECT("", "b", "text", "text");
	got = regsub("(", "b", "text", REG_EXTENDED);
	CHECK_NULL(got);
	EXPECT(".", "12345", "ab", "1234512345");
	EXPECT("é", "e", "café", "cafe");
	EXPECT("a", "ä", "banana", "bänänä");
}

TEST(regsub, whole_text_and_classes)
{
	EXPECT("^.*$", "[\\0]", "abc", "[abc]");
	EXPECT("[[:digit:]]+", "N", "a1b22c333", "aNbNcN");
	EXPECT("[^a-z]", "", "a-b_c d", "abcd");
}

TEST(regsub, output_grows_with_many_replacements)
{
	char	text[201], with[201], want[2001];
	u_int	i;

	memset(text, 'a', 200);
	text[200] = '\0';
	for (i = 0; i < 200; i++)
		memcpy(want + i * 10, "0123456789", 10);
	want[2000] = '\0';
	EXPECT("a", "0123456789", text, want);

	memset(with, 'b', 200);
	with[200] = '\0';
	memcpy(want, with, 200);
	want[200] = 'x';
	want[201] = '\0';
	EXPECT("^", with, "x", want);
}
