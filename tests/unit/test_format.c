#include <sys/types.h>
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "termo.h"
#include "test.h"
#include "harness.h"

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

/* "%h%y" without a %y literal: -Wformat=2 carries -Wformat-y2k on GCC. */
static void
month_year(const struct tm *tm, char *buf, size_t len)
{
	char	mon[8], year[8];

	strftime(mon, sizeof mon, "%h", tm);
	strftime(year, sizeof year, "%Y", tm);
	xsnprintf(buf, len, "%s%s", mon, year + 2);
}

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
	EXPECT("#{e|/|:5,0}", "inf");
	EXPECT("#{e|/|f:5,0}", "inf");
	EXPECT("#{e|/|:-5,0}", "-inf");
	got = format_expand(ft, "#{e|%|:5,0}");
	CHECK(strstr(got, "nan") != nullptr);
	free(got);
	format_free(ft);
}

TEST(format, loop_limit_returns_empty)
{
	setup();
	EXPECT("#{E:self}", "");
	format_add(ft, "x", "%s", "#{E:y}");
	format_add(ft, "y", "%s", "#{E:x}");
	EXPECT("#{E:x}", "");
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

TEST(format, variables_from_options_and_environ)
{
	setup();
	EXPECT("#{history-limit}", "2000");
	EXPECT("#{mouse}", "1");
	EXPECT("#{mode-keys}", "emacs");
	EXPECT("#{prefix}", "C-b");
	EXPECT("#{command-alias[0]}", "split-pane=split-window");
	EXPECT("#{command-alias[1]}", "splitp=split-window");
	EXPECT("#{command-alias[99]}", "");
	options_set_string(global_s_options, "@user", 0, "%s", "u1");
	EXPECT("#{@user}", "u1");
	EXPECT("#{@nope}", "");
	environ_set(global_environ, "TERMO_TEST_ENV", 0, "%s", "env1");
	EXPECT("#{TERMO_TEST_ENV}", "env1");
	EXPECT("#{t:TERMO_TEST_ENV}", "");
	environ_unset(global_environ, "TERMO_TEST_ENV");
	format_add(ft, "mouse", "%s", "tree");
	EXPECT("#{mouse}", "1");
	format_free(ft);
}

TEST(format, time_modifiers_and_expand_time)
{
	struct timeval	 tv = { .tv_sec = 1700000000 }, tv2 = { 0 };
	struct tm	 tm;
	time_t		 now = time(nullptr);
	char		 want[64], buf[64], *got;
	long long	 v;

	setup();
	format_add_tv(ft, "when", &tv);
	EXPECT("#{when}", "1700000000");

	ctime_r(&tv.tv_sec, want);
	want[strcspn(want, "\n")] = '\0';
	EXPECT("#{t:when}", want);

	localtime_r(&tv.tv_sec, &tm);
	strftime(want, sizeof want, "%Y-%m-%d", &tm);
	EXPECT("#{t/f/%Y-%m-%d:when}", want);
	month_year(&tm, want, sizeof want);
	EXPECT("#{t/p:when}", want);

	format_add(ft, "epoch", "%s", "1700000000");
	strftime(want, sizeof want, "%Y", &tm);
	EXPECT("#{t/f/%Y:epoch}", want);

	tv2.tv_sec = now - 90000;
	format_add_tv(ft, "recent", &tv2);
	EXPECT("#{t/r:recent}", "1d1h");
	got = format_expand(ft, "#{t/d:recent}");
	v = strtoll(got, nullptr, 10);
	CHECK(v >= 90000 && v <= 90002);
	free(got);

	EXPECT("#{t:a}", "");
	EXPECT("#{t:nope}", "");

	format_add(ft, "yfmt", "%s", "%Y");
	localtime_r(&now, &tm);
	strftime(want, sizeof want, "%Y", &tm);
	EXPECT("#{T:yfmt}", want);
	EXPECT("#{E:yfmt}", "%Y");

	xsnprintf(buf, sizeof buf, "%s-hello", want);
	got = format_expand_time(ft, "%Y-#{a}");
	CHECK_EQ(got, buf);
	free(got);
	EXPECT("%Y", "%Y");
	format_free(ft);
}

static void
pretty(const char *file, int line, time_t t, int seconds, const char *want)
{
	char	*got = format_pretty_time(t, seconds);

	if (strcmp(got, want) != 0) {
		test_fail(file, line, "pretty_time(now%+lld, %d): got \"%s\", "
		    "want \"%s\"", (long long)(t - time(nullptr)), seconds, got,
		    want);
	}
	free(got);
}
#define PRETTY(t, seconds, want) pretty(__FILE__, __LINE__, t, seconds, want)

TEST(format, pretty_time_buckets)
{
	time_t		now = time(nullptr), t;
	struct tm	tm;
	char		want[16];

	t = now - 60;
	localtime_r(&t, &tm);
	strftime(want, sizeof want, "%H:%M", &tm);
	PRETTY(t, 0, want);
	strftime(want, sizeof want, "%H:%M:%S", &tm);
	PRETTY(t, 1, want);

	t = now - 3 * 86400;
	localtime_r(&t, &tm);
	strftime(want, sizeof want, "%a%d", &tm);
	PRETTY(t, 0, want);

	t = now - 200 * 86400;
	localtime_r(&t, &tm);
	strftime(want, sizeof want, "%d%b", &tm);
	PRETTY(t, 0, want);

	t = now - 400 * 86400;
	localtime_r(&t, &tm);
	month_year(&tm, want, sizeof want);
	PRETTY(t, 0, want);

	t = now + 1000;
	localtime_r(&t, &tm);
	strftime(want, sizeof want, "%H:%M", &tm);
	PRETTY(t, 0, want);
}

TEST(format, basename_dirname_and_quote_variants)
{
	setup();
	format_add(ft, "path", "%s", "/usr/local/bin/termo");
	EXPECT("#{b:path}", "termo");
	EXPECT("#{d:path}", "/usr/local/bin");
	EXPECT("#{b:#{path}}", "/usr/local/bin/termo");
	format_add(ft, "sh", "%s", "a b;$c\"d'e");
	EXPECT("#{q:sh}", "a\\ b\\;\\$c\\\"d\\'e");
	EXPECT("#{q/s:sh}", "'a b;$c\"d'\\''e'");
	format_add(ft, "hash", "%s", "#{x}#");
	EXPECT("#{q/e:hash}", "##{x}##");
	EXPECT("#{q/h:hash}", "##{x}##");
	format_add(ft, "sp", "%s", "a b");
	EXPECT("#{q/a:sp}", "\"a b\"");
	EXPECT("#{q/a:a}", "hello");
	format_free(ft);
}

TEST(format, character_colour_and_width)
{
	setup();
	EXPECT("#{a:65}", "A");
	EXPECT("#{a:#{e|+|:60,5}}", "A");
	EXPECT("#{a:31}", "");
	EXPECT("#{a:x}", "");
	EXPECT("#{c:#ff0000}", "ff0000");
	EXPECT("#{c:red}", "800000");
	EXPECT("#{c:nope}", "");
	EXPECT("#{c/f:#ff0000}", "\033[38;2;255;0;0m");
	EXPECT("#{c/b:#ff0000}", "\033[48;2;255;0;0m");
	EXPECT("#{c/f:red}", "\033[31m");
	EXPECT("#{c/f:none}", "\033[0m");
	format_add(ft, "wide", "%s", "日本");
	EXPECT("#{w:wide}", "4");
	EXPECT("#{n:wide}", "6");
	EXPECT("#{p5:wide}", "日本 ");
	EXPECT("#{=1:wide}", "");
	EXPECT("#{=2:wide}", "日");
	EXPECT("#{=-2:wide}", "本");
	format_free(ft);
}

TEST(format, boolean_and_comparison_operators)
{
	setup();
	EXPECT("#{!:#{a}}", "0");
	EXPECT("#{!:#{empty}}", "1");
	EXPECT("#{!:0}", "1");
	EXPECT("#{!!:#{a}}", "1");
	EXPECT("#{!!:#{nope}}", "0");
	EXPECT("#{>:2,1}", "1");
	EXPECT("#{<=:1,1}", "1");
	EXPECT("#{<:10,9}", "1");
	EXPECT("#{e|<|:10,9}", "0");
	EXPECT("#{||:0,0,1}", "1");
	EXPECT("#{&&:1,1,0}", "0");
	EXPECT("#{==:a#,b,a#,b}", "1");
	EXPECT("#{m/i:HEL*,#{a}}", "1");
	EXPECT("#{m/ri:^HELLO$,#{a}}", "1");
	EXPECT("#{m/z:hlo,#{a}}", "1");
	EXPECT("#{m/z:xyz,#{a}}", "0");
	EXPECT("#{m/p:hlo,#{a}}", "0,3,4");
	EXPECT("#{s/#{a}/X/:a}", "X");
	format_free(ft);
}

TEST(format, expression_comparisons_and_errors)
{
	setup();
	EXPECT("#{e|==|:2,2}", "1");
	EXPECT("#{e|!=|:2,3}", "1");
	EXPECT("#{e|>|:3,2}", "1");
	EXPECT("#{e|>=|:2,2}", "1");
	EXPECT("#{e|<=|:1,2}", "1");
	EXPECT("#{e|m|:7,2}", "1");
	EXPECT("#{e|/|f|1:1,3}", "0.3");
	EXPECT("#{e|+|f|0:1.4,1.4}", "3");
	EXPECT("#{e|^|:1,2}", "");
	EXPECT("#{e|+|:x,1}", "");
	EXPECT("#{e|+||101:1,1}", "");
	EXPECT("#{e|+|:1}", "");
	format_free(ft);
}

TEST(format, conditionals_multi_arm_and_option_conditions)
{
	setup();
	EXPECT("#{?empty,A,a,B,C}", "B");
	EXPECT("#{?empty,A,nope,B}", "");
	EXPECT("#{?empty,A,nope,B,D}", "D");
	EXPECT("#{?mouse,on,off}", "on");
	options_set_number(global_s_options, "mouse", 0);
	EXPECT("#{?mouse,on,off}", "off");
	EXPECT("#{?#{e|>|:#{num},40},big,small}", "big");
	EXPECT("#{?a,#{a}#,x,y}", "hello,x");
	format_free(ft);
}

TEST(format, aliases_escapes_and_nojobs)
{
	struct format_tree	*ft2;
	char			*got, *want;

	setup();
	got = format_expand(ft, "#H");
	want = format_expand(ft, "#{host}");
	CHECK(*want != '\0');
	CHECK_EQ(got, want);
	free(got);
	free(want);
	got = format_expand(ft, "#h");
	want = format_expand(ft, "#{host_short}");
	CHECK_EQ(got, want);
	CHECK_NULL(strchr(got, '.'));
	free(got);
	free(want);
	EXPECT("#S", "");
	EXPECT("#Q", "#Q");
	EXPECT("#,#}", ",}");
	EXPECT("#[fg=red]x", "#[fg=red]x");
	EXPECT("##[x", "##[x");
	EXPECT("#(echo", "");
	format_free(ft);

	ft2 = format_create(nullptr, nullptr, FORMAT_NONE, FORMAT_NOJOBS);
	got = format_expand(ft2, "a#(echo hi)b");
	CHECK_EQ(got, "ab");
	free(got);
	format_free(ft2);
}

TEST(format, repeat_and_cycle)
{
	struct format_tree	*sft;
	char			*got;

	setup();
	EXPECT("#{R:ab,3}", "ababab");
	EXPECT("#{R:ab,0}", "");
	EXPECT("x#{R:ab}y", "x");
	EXPECT("#{A:one,two}", "");
	format_free(ft);

	sft = format_create(nullptr, nullptr, FORMAT_NONE, FORMAT_STATUS);
	got = format_expand(sft, "#{A:only}");
	CHECK_EQ(got, "only");
	free(got);
	got = format_expand(sft, "#{A:one,two}");
	CHECK(strcmp(got, "one") == 0 || strcmp(got, "two") == 0);
	free(got);
	got = format_expand(sft, "#{A/2:one,two}");
	CHECK(strcmp(got, "one") == 0 || strcmp(got, "two") == 0);
	free(got);
	got = format_expand(sft, "#{A:}");
	CHECK_EQ(got, "");
	free(got);
	format_free(sft);
}

TEST(format, client_and_session_lookups_without_them)
{
	setup();
	EXPECT("#{I/c:RGB}", "");
	EXPECT("#{I/f:RGB}", "");
	EXPECT("#{I/e:HOME}", "");
	EXPECT("#{L:x}", "");
	EXPECT("#{C:x}", "0");
	EXPECT("#{N/s:zzz}", "0");
	EXPECT("a#{W:x}b", "a");
	EXPECT("a#{P:x}b", "a");
	EXPECT("a#{N/w:x}b", "a");
	format_free(ft);
}

static int	cb_calls;

static void *
cb_value([[maybe_unused]] struct format_tree *tree)
{
	cb_calls++;
	return (xstrdup("cbv"));
}

struct seen_keys {
	int	a, num, when, cbk, host;
};

static void
collect(const char *key, const char *value, void *arg)
{
	struct seen_keys	*seen = arg;

	if (strcmp(key, "a") == 0 && strcmp(value, "again") == 0)
		seen->a = 1;
	if (strcmp(key, "num") == 0 && strcmp(value, "42") == 0)
		seen->num = 1;
	if (strcmp(key, "when") == 0 && strcmp(value, "1700000000") == 0)
		seen->when = 1;
	if (strcmp(key, "cbk") == 0 && strcmp(value, "cbv") == 0)
		seen->cbk = 1;
	if (strcmp(key, "host") == 0)
		seen->host = 1;
}

TEST(format, add_cb_each_merge_and_overwrite)
{
	struct format_tree	*dst;
	struct timeval		 tv = { .tv_sec = 1700000000 };
	struct seen_keys	 seen = { 0 };
	char			*got;

	setup();
	cb_calls = 0;
	format_add_cb(ft, "cbk", cb_value);
	EXPECT("#{cbk}#{cbk}", "cbvcbv");
	CHECK_EQ(cb_calls, 1);
	format_add(ft, "a", "%s", "again");
	EXPECT("#{a}", "again");
	format_add_tv(ft, "when", &tv);

	format_each(ft, collect, &seen);
	CHECK(seen.a);
	CHECK(seen.num);
	CHECK(seen.when);
	CHECK(seen.cbk);
	CHECK(seen.host);
	CHECK_EQ(cb_calls, 1);

	format_add_cb(ft, "cb2", cb_value);
	dst = format_create(nullptr, nullptr, FORMAT_NONE, 0);
	format_merge(dst, ft);
	got = format_expand(dst, "#{a}|#{cbk}|#{when}|#{cb2}");
	CHECK_EQ(got, "again|cbv||");
	free(got);
	CHECK_EQ(cb_calls, 1);
	CHECK_NULL(format_get_pane(ft));
	format_free(dst);
	format_free(ft);
}

TEST(format, pane_search_pane_loop_and_pane_options)
{
	struct window		*w = termo_test_window(40, 5, 2);
	struct window_pane	*wp = w->active;
	struct screen_write_ctx	 ctx;
	struct options_entry	*o;
	char			*got, *want;

	screen_write_start(&ctx, &wp->base);
	screen_write_puts(&ctx, &grid_default_cell, "hello world");
	screen_write_cursormove(&ctx, 0, 2, 0);
	screen_write_puts(&ctx, &grid_default_cell, "third");
	screen_write_stop(&ctx);

	setup();
	format_defaults(ft, nullptr, nullptr, nullptr, wp);
	EXPECT("#{C:hello}", "1");
	EXPECT("#{C/r:^hello w.*d$}", "1");
	EXPECT("#{C/i:HELLO}", "1");
	EXPECT("#{C:third}", "3");
	EXPECT("#{C:zzz}", "0");
	EXPECT("#{P:#{pane_active}#,}", "1,0,");
	EXPECT("#{P/r:#{pane_active}#,}", "0,1,");
	EXPECT("#{P:#{loop_index}#{?loop_last_flag,!,;}}", "0;1!");
	EXPECT("#{P:x,y}", "yx");
	EXPECT("#{pane_index}", "0");
	EXPECT("#{window_panes}", "2");
	got = format_expand(ft, "#D");
	want = format_expand(ft, "#{pane_id}");
	CHECK_EQ(got, want);
	CHECK(want[0] == '%');
	free(got);
	free(want);
	CHECK(format_get_pane(ft) == wp);

	options_set_string(wp->options, "@p", 0, "%s", "1");
	EXPECT("#{O/p:#{option_name}=#{option_value},}", "@p=1,");
	options_set_string(w->options, "@w", 0, "%s", "1");
	o = options_default(w->options, options_search("pane-colours"));
	CHECK_EQ(options_array_set(o, "0", "red", 0, nullptr), 0);
	CHECK_EQ(options_array_set(o, "1", "blue", 0, nullptr), 0);
	EXPECT("#{O/w:#{option_name}[#{option_array_index}]=#{option_value}"
	    "#{?#{option_is_user},u,}#{?#{option_array_last},!,;}}",
	    "@w[]=1u;pane-colours[0]=red;pane-colours[1]=blue!");

	format_free(ft);
	termo_test_window_free(w);
}

TEST(format, session_window_loops_and_name_checks)
{
	struct window	*w = termo_test_window(20, 5, 1), *w2;
	struct session	*s;
	char		*cause;

	free(w->name);
	w->name = xstrdup("win");
	s = termo_test_session("fmt", w);

	setup();
	format_defaults(ft, nullptr, s, s->curw, nullptr);
	EXPECT("#S", "fmt");
	EXPECT("#I", "0");
	EXPECT("#W", "win");
	EXPECT("#{session_windows}", "1");
	EXPECT("#{W:#{window_index}#,}", "0,");
	EXPECT("#{W:a,b}", "b");
	EXPECT("#{S:#{?#{==:#{session_name},fmt},#{session_name},}}", "fmt");
	EXPECT("#{N/s:fmt}", "1");
	EXPECT("#{N/s:zz}", "0");
	EXPECT("#{N/w:win}", "1");
	EXPECT("#{N/w:zz}", "0");

	options_set_string(s->options, "@s", 0, "%s", "1");
	EXPECT("#{O/s:#{option_name}=#{option_value},}", "@s=1,");
	environ_set(s->environ, "SV", 0, "%s", "1");
	environ_set(s->environ, "SH", ENVIRON_HIDDEN, "%s", "2");
	EXPECT("#{V:#{environ_name}#{?environ_hidden,*,},}", "SH*,SV,");

	w2 = termo_test_window(20, 5, 1);
	CHECK_NONNULL(session_attach(s, w2, -1, &cause));
	EXPECT("#{W:#{window_index}#{?window_active,*,}#,}", "0*,1,");
	EXPECT("#{W/r:#{window_index}#,}", "1,0,");
	EXPECT("#{W:#{window_before_active}#{window_after_active}#,}",
	    "00,01,");
	EXPECT("#{W:#{next_window_index}|#{prev_window_index}#,}", "1|,|0,");

	format_free(ft);
	termo_test_session_free(s);
	termo_test_window_free(w);
	termo_test_window_free(w2);
}

TEST(format, grid_word_line_and_hyperlink)
{
	struct grid		*gd = grid_create(20, 3, 0);
	struct grid_cell	 gc;
	struct screen		 s;
	u_int			 link;
	char			*got;

	grid_set_cells(gd, 0, 0, &grid_default_cell, "foo bar-baz", 11);
	got = format_grid_word(gd, 1, 0);
	CHECK_EQ(got, "foo");
	free(got);
	got = format_grid_word(gd, 5, 0);
	CHECK_EQ(got, "bar");
	free(got);
	got = format_grid_word(gd, 3, 0);
	CHECK_EQ(got, "bar");
	free(got);
	got = format_grid_line(gd, 0);
	CHECK_EQ(got, "foo bar-baz");
	free(got);
	CHECK_NULL(format_grid_line(gd, 1));

	grid_get_line(gd, 0)->flags |= GRID_LINE_WRAPPED;
	grid_set_cells(gd, 17, 0, &grid_default_cell, "abc", 3);
	grid_set_cells(gd, 0, 1, &grid_default_cell, "def", 3);
	got = format_grid_word(gd, 1, 1);
	CHECK_EQ(got, "abcdef");
	free(got);

	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.flags |= GRID_FLAG_TAB;
	grid_set_cells(gd, 0, 2, &grid_default_cell, "a", 1);
	grid_set_cell(gd, 1, 2, &gc);
	grid_set_cells(gd, 2, 2, &grid_default_cell, "b", 1);
	got = format_grid_line(gd, 2);
	CHECK_EQ(got, "a\tb");
	free(got);
	got = format_grid_word(gd, 2, 2);
	CHECK_EQ(got, "b");
	free(got);
	grid_destroy(gd);

	screen_init(&s, 20, 1, 0);
	screen_reset_hyperlinks(&s);
	link = hyperlinks_put(s.hyperlinks, "http://x.example", nullptr);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.link = link;
	grid_set_cell(s.grid, 2, 0, &gc);
	got = format_grid_hyperlink(s.grid, 2, 0, &s);
	CHECK_EQ(got, "http://x.example");
	free(got);
	termo_test_wide(&gc, "日");
	gc.link = link;
	grid_set_cell(s.grid, 4, 0, &gc);
	grid_set_padding(s.grid, 5, 0, 8);
	got = format_grid_hyperlink(s.grid, 5, 0, &s);
	CHECK_EQ(got, "http://x.example");
	free(got);
	CHECK_NULL(format_grid_hyperlink(s.grid, 0, 0, &s));
	screen_free(&s);
}
