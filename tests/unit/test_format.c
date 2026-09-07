#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

static struct format_tree	*ft;

static void
setup(void)
{
	ft = format_create(nullptr, nullptr, FORMAT_NONE, 0);
	format_add(ft, "a", "%s", "hello");
	format_add(ft, "empty", "%s", "");
	format_add(ft, "num", "%d", 42);
	format_add(ft, "self", "%s", "#{E:self}");
}

static void
expect(const char *file, int line, const char *fmt, const char *want)
{
	char	*got = format_expand(ft, fmt);

	if (strcmp(got, want) != 0)
		test_fail(file, line, "%s: got \"%s\", want \"%s\"", fmt, got, want);
	free(got);
}
#define EXPECT(fmt, want) expect(__FILE__, __LINE__, fmt, want)

TEST(format, variables_and_missing)
{
	setup();
	EXPECT("#{a}", "hello");
	EXPECT("x#{a}y", "xhelloy");
	EXPECT("#{nope}", "");
	EXPECT("plain", "plain");
	EXPECT("##", "#");
	format_free(ft);
}

TEST(format, length_truncate_pad)
{
	setup();
	EXPECT("#{n:a}", "5");
	EXPECT("#{=3:a}", "hel");
	EXPECT("#{=-3:a}", "llo");
	EXPECT("#{=|3|...:a}", "hel...");
	EXPECT("#{=|-3|~:a}", "~llo");
	EXPECT("#{=|10|...:a}", "hello");
	EXPECT("#{p7:a}", "hello  ");
	EXPECT("#{p-7:a}", "  hello");
	EXPECT("#{p2:a}", "hello");
	format_free(ft);
}

TEST(format, substitute)
{
	setup();
	EXPECT("#{s/l/L/:a}", "heLLo");
	EXPECT("#{s/L/x/i:a}", "hexxo");
	EXPECT("#{s/^h(.)/\\1H/:a}", "eHllo");
	EXPECT("#{s/(/x/:a}", "hello");
	format_free(ft);
}

TEST(format, conditionals_and_comparisons)
{
	setup();
	EXPECT("#{?a,yes,no}", "yes");
	EXPECT("#{?empty,yes,no}", "no");
	EXPECT("#{?nope,yes,no}", "no");
	EXPECT("#{==:#{a},hello}", "1");
	EXPECT("#{!=:#{a},hello}", "0");
	EXPECT("#{<:1,2}", "1");
	EXPECT("#{>=:2,2}", "1");
	EXPECT("#{||:0,1}", "1");
	EXPECT("#{&&:1,0}", "0");
	EXPECT("#{?#{==:#{a},hello},T,F}", "T");
	EXPECT("#{m:h*,#{a}}", "1");
	EXPECT("#{m/r:^h.l+o$,#{a}}", "1");
	EXPECT("#{m/r:[,#{a}}", "0");
	format_free(ft);
}

TEST(format, expressions)
{
	setup();
	EXPECT("#{e|+|:2,3}", "5");
	EXPECT("#{e|-|:2,3}", "-1");
	EXPECT("#{e|*|:2,3}", "6");
	EXPECT("#{e|/|:6,3}", "2");
	EXPECT("#{e|/|:7,2}", "3");
	EXPECT("#{e|%|:7,2}", "1");
	EXPECT("#{e|/|f|2:5,2}", "2.50");
	EXPECT("#{e|*|f:2.5,2}", "5.00");
	EXPECT("#{e|+|:#{num},1}", "43");
	format_free(ft);
}

/* Regression: inf was cast to long long, undefined behaviour. */
TEST(format, division_by_zero_does_not_crash)
{
	char	*got;

	setup();
	got = format_expand(ft, "#{e|/|:5,0}");
	CHECK_NONNULL(got);
	free(got);
	got = format_expand(ft, "#{e|/|f:5,0}");
	CHECK_NONNULL(got);
	free(got);
	got = format_expand(ft, "#{e|%|:5,0}");
	CHECK_NONNULL(got);
	free(got);
	format_free(ft);
}

TEST(format, recursion_terminates)
{
	char	*got;

	setup();
	got = format_expand(ft, "#{E:self}");
	CHECK_NONNULL(got);
	free(got);
	EXPECT("#{?#{?#{?#{?#{?a,1,0},1,0},1,0},1,0},deep,shallow}", "deep");
	format_free(ft);
}

TEST(format, literal_and_unterminated)
{
	setup();
	EXPECT("#{l:#{a}}", "#{a}");
	EXPECT("#{a", "");
	EXPECT("#{", "");
	EXPECT("#", "");
	format_free(ft);
}

TEST(format, true_and_skip)
{
	CHECK_EQ(format_true("1"), 1);
	CHECK_EQ(format_true("0"), 0);
	CHECK_EQ(format_true(""), 0);
	CHECK_EQ(format_true("abc"), 1);
	CHECK_EQ(format_skip("#{a},b", ","), ",b");
	CHECK_EQ(format_skip("#{x,y},b", ","), ",b");
	CHECK_NULL(format_skip("#{a}", ","));
}
