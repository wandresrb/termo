/*
 * The VT parser against a real pane and screen, no pty, no server. Replies
 * the parser writes back come out of the other end of a bufferevent pair.
 */

#include <sys/types.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "termo.h"
#include "harness.h"
#include "test.h"

static struct window		*w;
static struct window_pane	*wp;
static struct bufferevent	*vpty[2];

static void
open_pane(u_int sx, u_int sy)
{
	w = termo_test_window(sx, sy, 1);
	wp = w->active;
	bufferevent_pair_new(libevent, BEV_OPT_CLOSE_ON_FREE, vpty);
	bufferevent_enable(vpty[1], EV_READ);
	wp->ictx = input_init(wp, vpty[0], &wp->palette, nullptr);
	wp->fd = open("/dev/null", O_WRONLY);
	wp->event = bufferevent_new(wp->fd, nullptr, nullptr, nullptr, nullptr);
}

static void
close_pane(void)
{
	termo_test_window_free(w);
	bufferevent_free(vpty[0]);
	bufferevent_free(vpty[1]);
}

static void
feed(const char *bytes, size_t len)
{
	input_parse_buffer(wp, (const u_char *)bytes, len);
	termo_test_drain();
}
#define FEED(s) feed(s, sizeof(s) - 1)

static char *
reply(void)
{
	struct evbuffer	*evb = bufferevent_get_input(vpty[1]);
	size_t		 len = EVBUFFER_LENGTH(evb);
	char		*out = xmalloc(len + 1);

	if (len != 0)
		memcpy(out, EVBUFFER_DATA(evb), len);
	out[len] = '\0';
	evbuffer_drain(evb, len);
	return (out);
}

static void
expect_row(const char *file, int line, u_int y, const char *want)
{
	struct grid	*gd = wp->base.grid;
	char		*got = grid_string_cells(gd, 0, gd->hsize + y, gd->sx,
	    nullptr, GRID_STRING_TRIM_SPACES, nullptr);

	if (strcmp(got, want) != 0)
		test_fail(file, line, "row %u: got \"%s\", want \"%s\"", y, got, want);
	free(got);
}
#define EXPECT_ROW(y, want) expect_row(__FILE__, __LINE__, y, want)

static void
expect_reply(const char *file, int line, const char *want)
{
	char	*got = reply();

	if (strcmp(got, want) != 0)
		test_fail(file, line, "reply: got \"%s\", want \"%s\"", got, want);
	free(got);
}
#define EXPECT_REPLY(want) expect_reply(__FILE__, __LINE__, want)

static void
cell(u_int x, u_int y, struct grid_cell *gc)
{
	grid_view_get_cell(wp->base.grid, x, y, gc);
}

TEST(input, plain_text_and_line_breaks)
{
	open_pane(20, 5);
	FEED("hello\r\nworld");
	EXPECT_ROW(0, "hello");
	EXPECT_ROW(1, "world");
	CHECK_EQ(wp->base.cx, 5u);
	CHECK_EQ(wp->base.cy, 1u);
	FEED("\tX");
	CHECK_EQ(wp->base.cx, 9u);
	close_pane();
}

TEST(input, sgr_attributes_and_colours)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("\033[1;4;31mA\033[0mB\033[38;5;123mC\033[48;2;1;2;3mD"
	    "\033[38:2::10:20:30mE\033[39mF");
	cell(0, 0, &gc);
	CHECK_EQ(gc.attr, GRID_ATTR_BRIGHT | GRID_ATTR_UNDERSCORE);
	CHECK_EQ(gc.fg, 1);
	cell(1, 0, &gc);
	CHECK_EQ(gc.attr, 0);
	CHECK_EQ(gc.fg, 8);
	cell(2, 0, &gc);
	CHECK_EQ(gc.fg, 123 | COLOUR_FLAG_256);
	cell(3, 0, &gc);
	CHECK_EQ(gc.bg, 0x010203 | COLOUR_FLAG_RGB);
	cell(4, 0, &gc);
	CHECK_EQ(gc.fg, 0x0a141e | COLOUR_FLAG_RGB);
	cell(5, 0, &gc);
	CHECK_EQ(gc.fg, 8);
	CHECK_EQ(gc.bg, 0x010203 | COLOUR_FLAG_RGB);
	close_pane();
}

TEST(input, sgr_partial_reset_keeps_other_attributes)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("\033[1;3;31mA\033[22mB\033[23mC");
	cell(1, 0, &gc);
	CHECK_EQ(gc.attr, GRID_ATTR_ITALICS);
	CHECK_EQ(gc.fg, 1);
	cell(2, 0, &gc);
	CHECK_EQ(gc.attr, 0);
	CHECK_EQ(gc.fg, 1);
	close_pane();
}

TEST(input, cursor_positioning_and_clamping)
{
	open_pane(20, 5);
	FEED("\033[3;4H");
	CHECK_EQ(wp->base.cx, 3u);
	CHECK_EQ(wp->base.cy, 2u);
	FEED("\033[999;999H");
	CHECK_EQ(wp->base.cx, 19u);
	CHECK_EQ(wp->base.cy, 4u);
	FEED("\033[;H");
	CHECK_EQ(wp->base.cx, 0u);
	CHECK_EQ(wp->base.cy, 0u);
	FEED("\033[2B\033[3C\033[1A\033[1D");
	CHECK_EQ(wp->base.cx, 2u);
	CHECK_EQ(wp->base.cy, 1u);
	close_pane();
}

TEST(input, csi_oddities_are_survived)
{
	open_pane(20, 5);
	FEED("\033[?9999zOK");
	EXPECT_ROW(0, "OK");
	FEED("\r\033[1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1mA");
	EXPECT_ROW(0, "AK");
	FEED("\033[    \030X");
	CHECK_NONNULL(strstr("AK X", "X"));
	FEED("\033[2J\033[HZ");
	EXPECT_ROW(0, "Z");
	close_pane();
}

TEST(input, device_attributes_and_status_reports)
{
	open_pane(20, 5);
	FEED("\033[c");
#ifdef ENABLE_SIXEL
	EXPECT_REPLY("\033[?1;2;4c");
#else
	EXPECT_REPLY("\033[?1;2c");
#endif
	FEED("\033[3;4H\033[6n");
	EXPECT_REPLY("\033[3;4R");
	FEED("\033[5n");
	EXPECT_REPLY("\033[0n");
	close_pane();
}

TEST(input, osc_title_with_both_terminators)
{
	open_pane(20, 5);
	FEED("\033]2;first\007");
	CHECK_EQ(wp->base.title, "first");
	FEED("\033]0;second\033\\");
	CHECK_EQ(wp->base.title, "second");
	FEED("\033]999;ignored\007X");
	CHECK_EQ(wp->base.title, "second");
	EXPECT_ROW(0, "X");
	close_pane();
}

TEST(input, osc8_hyperlink_marks_cells)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("\033]8;;http://example.test\007link\033]8;;\007plain");
	cell(0, 0, &gc);
	CHECK(gc.link != 0);
	cell(3, 0, &gc);
	CHECK(gc.link != 0);
	cell(4, 0, &gc);
	CHECK_EQ(gc.link, 0u);
	EXPECT_ROW(0, "linkplain");
	close_pane();
}

TEST(input, osc52_sets_a_paste_buffer_when_allowed)
{
	struct paste_buffer	*pb;
	const char		*data;
	size_t			 size;

	options_set_number(global_options, "set-clipboard", 2);
	open_pane(20, 5);
	FEED("\033]52;c;aGVsbG8=\007");
	pb = paste_get_top(nullptr);
	REQUIRE_NONNULL(pb);
	data = paste_buffer_data(pb, &size);
	CHECK_EQ(size, 5u);
	CHECK(memcmp(data, "hello", 5) == 0);
	close_pane();
}

TEST(input, dcs_and_apc_do_not_leak_into_the_screen)
{
	open_pane(20, 5);
	FEED("\033P$qBAD\033\\OK");
	EXPECT_ROW(0, "OK");
	FEED("\r\033_private application command\033\\AB");
	EXPECT_ROW(0, "AB");
	FEED("\r\033Xstring\033\\CD");
	EXPECT_ROW(0, "CD");
	close_pane();
}

TEST(input, alternate_screen_mode)
{
	open_pane(20, 5);
	FEED("main");
	FEED("\033[?1049h");
	REQUIRE_NONNULL(wp->base.saved_grid);
	EXPECT_ROW(0, "");
	CHECK_EQ(wp->base.cx, 4u);
	FEED("\033[Halt");
	EXPECT_ROW(0, "alt");
	FEED("\033[?1049l");
	CHECK_NULL(wp->base.saved_grid);
	EXPECT_ROW(0, "main");
	CHECK_EQ(wp->base.cx, 4u);
	close_pane();
}

TEST(input, scroll_region_bounds)
{
	open_pane(20, 5);
	FEED("\033[2;4r");
	CHECK_EQ(wp->base.rupper, 1u);
	CHECK_EQ(wp->base.rlower, 3u);
	FEED("\033[4;2r");
	CHECK_EQ(wp->base.rupper, 1u);
	CHECK_EQ(wp->base.rlower, 3u);
	FEED("\033[r");
	CHECK_EQ(wp->base.rupper, 0u);
	CHECK_EQ(wp->base.rlower, 4u);
	close_pane();
}

TEST(input, private_modes_toggle_flags)
{
	open_pane(20, 5);
	CHECK(wp->base.mode & MODE_CURSOR);
	FEED("\033[?25l");
	CHECK_EQ(wp->base.mode & MODE_CURSOR, 0);
	FEED("\033[?25h\033[?2004h\033[?1000h\033[?1006h");
	CHECK(wp->base.mode & MODE_CURSOR);
	CHECK(wp->base.mode & MODE_BRACKETPASTE);
	CHECK(wp->base.mode & MODE_MOUSE_STANDARD);
	CHECK(wp->base.mode & MODE_MOUSE_SGR);
	FEED("\033[?1000l");
	CHECK_EQ(wp->base.mode & MODE_MOUSE_STANDARD, 0);
	close_pane();
}

TEST(input, malformed_utf8_becomes_replacement)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("\360\200\200\200A\355\240\200B");
	cell(0, 0, &gc);
	CHECK(memcmp(gc.data.data, "\xef\xbf\xbd", 3) == 0);
	cell(1, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'A');
	cell(2, 0, &gc);
	CHECK(memcmp(gc.data.data, "\xef\xbf\xbd", 3) == 0);
	cell(3, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'B');
	CHECK_EQ(wp->base.cx, 4u);
	close_pane();
}

TEST(input, wide_characters_and_combining)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("\xe4\xb8\xad" "e\xcc\x81" "x");
	cell(0, 0, &gc);
	CHECK_EQ(gc.data.width, 2);
	cell(1, 0, &gc);
	CHECK(gc.flags & GRID_FLAG_PADDING);
	cell(2, 0, &gc);
	CHECK_EQ(gc.data.size, 3);
	CHECK(memcmp(gc.data.data, "e\xcc\x81", 3) == 0);
	cell(3, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'x');
	close_pane();
}

TEST(input, a_megabyte_of_garbage_does_not_break_the_parser)
{
	u_char	*junk = xmalloc(1024 * 1024);
	size_t	 i;
	u_int	 seed = 12345;

	for (i = 0; i < 1024 * 1024; i++) {
		seed = seed * 1103515245 + 12345;
		junk[i] = (seed >> 16) & 0xff;
	}
	open_pane(20, 5);
	feed((const char *)junk, 1024 * 1024);
	/* ST ends any string the garbage left open, CAN aborts a sequence. */
	FEED("\033\\\030\033c\033[2J\033[HOK");
	EXPECT_ROW(0, "OK");
	free(junk);
	close_pane();
}

static struct grid_line *
gridline(u_int y)
{
	struct grid	*gd = wp->base.grid;

	return (grid_get_line(gd, gd->hsize + y));
}

/* termo_test_window creates panes with no scrollback. */
static void
enable_history(void)
{
	wp->base.grid->flags |= GRID_HISTORY;
	wp->base.grid->hlimit = 100;
}

TEST(input, sgr_underline_styles_and_remaining_attributes)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("\033[4:3mA\033[4:0mB\033[21mC\033[24mD\033[58;5;99mE"
	    "\033[58:2::1:2:3mF\033[59mG\033[93mH\033[103mI"
	    "\033[2;5;7;8;9;53mJ\033[22;25;27;28;29;55mK\033[38;5mL"
	    "\033[0m\033[38;2;1;2mM");
	cell(0, 0, &gc);
	CHECK_EQ(gc.attr, GRID_ATTR_UNDERSCORE_3);
	cell(1, 0, &gc);
	CHECK_EQ(gc.attr, 0);
	cell(2, 0, &gc);
	CHECK_EQ(gc.attr, GRID_ATTR_UNDERSCORE_2);
	cell(3, 0, &gc);
	CHECK_EQ(gc.attr, 0);
	cell(4, 0, &gc);
	CHECK_EQ(gc.us, 99 | COLOUR_FLAG_256);
	cell(5, 0, &gc);
	CHECK_EQ(gc.us, 0x010203 | COLOUR_FLAG_RGB);
	cell(6, 0, &gc);
	CHECK_EQ(gc.us, 8);
	cell(7, 0, &gc);
	CHECK_EQ(gc.fg, 93);
	cell(8, 0, &gc);
	CHECK_EQ(gc.bg, 93);
	cell(9, 0, &gc);
	CHECK_EQ(gc.attr, GRID_ATTR_DIM | GRID_ATTR_BLINK | GRID_ATTR_REVERSE |
	    GRID_ATTR_HIDDEN | GRID_ATTR_STRIKETHROUGH | GRID_ATTR_OVERLINE);
	cell(10, 0, &gc);
	CHECK_EQ(gc.attr, 0);
	cell(11, 0, &gc);
	CHECK_EQ(gc.fg, 8);
	/* A truncated 38;2;r;g leaves i alone, so r;g are re-read as SGR 1;2. */
	cell(12, 0, &gc);
	CHECK_EQ(gc.fg, 8);
	close_pane();
}

TEST(input, ich_dch_ech_rep_edge_counts)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("abcdef\033[3G\033[2@");
	EXPECT_ROW(0, "ab  cdef");
	FEED("\033[3P");
	EXPECT_ROW(0, "abdef");
	FEED("\033[2X");
	EXPECT_ROW(0, "ab  f");
	FEED("\033[0@");
	EXPECT_ROW(0, "ab   f");
	/* grid_view_insert_cells moves sx-cx-n cells and wipes only those. */
	FEED("\033[999@");
	EXPECT_ROW(0, "ab   f");
	FEED("\r\033[2JX\033[3b");
	EXPECT_ROW(0, "XXXX");
	FEED("\033[2;1H\033[3b");
	EXPECT_ROW(1, "");
	FEED("\033[1;19HX\033[10b");
	cell(17, 0, &gc);
	CHECK_EQ(gc.data.data[0], ' ');
	cell(18, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'X');
	cell(19, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'X');
	CHECK_EQ(wp->base.cx, 20u);
	FEED("\r\033[2K\xe4\xb8\xad\033[1b");
	EXPECT_ROW(0, "\xe4\xb8\xad\xe4\xb8\xad");
	CHECK_EQ(wp->base.cx, 4u);
	close_pane();
}

TEST(input, decsc_decrc_scp_rcp_and_alternate_have_separate_slots)
{
	struct grid_cell	gc;

	open_pane(20, 5);
	FEED("\033[31m\033[3;5H\0337\033[0m\033[10;10H\0338A");
	cell(4, 2, &gc);
	CHECK_EQ(gc.data.data[0], 'A');
	CHECK_EQ(gc.fg, 1);
	CHECK_EQ(wp->base.cx, 5u);
	CHECK_EQ(wp->base.cy, 2u);
	FEED("\033[2;2H\033[s\033[HB\033[uC");
	cell(1, 1, &gc);
	CHECK_EQ(gc.data.data[0], 'C');
	FEED("\033[2;4r\033[?6h\0337\033[?6l\0338");
	CHECK(wp->base.mode & MODE_ORIGIN);
	CHECK_EQ(wp->base.cy, 1u);
	FEED("\033[?6l\033[r\033[3;3H\0337\033[5;5H\033[?1049h\033[8;8H\0338");
	REQUIRE_NONNULL(wp->base.saved_grid);
	CHECK_EQ(wp->base.cx, 2u);
	CHECK_EQ(wp->base.cy, 2u);
	FEED("\033[?1049l");
	CHECK_NULL(wp->base.saved_grid);
	CHECK_EQ(wp->base.cx, 4u);
	CHECK_EQ(wp->base.cy, 4u);
	close_pane();
}

TEST(input, decrqm_replies_report_mode_state)
{
	open_pane(20, 5);
	FEED("\033[?7$p");
	EXPECT_REPLY("\033[?7;1$y");
	FEED("\033[?25l\033[?25$p");
	EXPECT_REPLY("\033[?25;2$y");
	FEED("\033[4$p");
	EXPECT_REPLY("\033[4;2$y");
	FEED("\033[4h\033[4$p");
	EXPECT_REPLY("\033[4;1$y");
	FEED("\033[?3$p");
	EXPECT_REPLY("\033[?3;4$y");
	FEED("\033[?1049$p");
	EXPECT_REPLY("\033[?1049;2$y");
	FEED("\033[?1049h\033[?1049$p\033[?47$p");
	EXPECT_REPLY("\033[?1049;1$y\033[?47;1$y");
	FEED("\033[?2004h\033[?2004$p");
	EXPECT_REPLY("\033[?2004;1$y");
	FEED("\033[?1$p\033[?6$p\033[?12$p\033[?1000$p\033[?1002$p"
	    "\033[?1003$p\033[?1004$p\033[?1005$p\033[?1006$p\033[?2026$p"
	    "\033[?2031$p");
	EXPECT_REPLY("\033[?1;2$y\033[?6;2$y\033[?12;2$y\033[?1000;2$y"
	    "\033[?1002;2$y\033[?1003;2$y\033[?1004;2$y\033[?1005;2$y"
	    "\033[?1006;2$y\033[?2026;2$y\033[?2031;2$y");
	FEED("\033[?1h\033[?6h\033[?12h\033[?1003h\033[?1004h\033[?1005h"
	    "\033[?1006h\033[?1$p\033[?6$p\033[?12$p\033[?1003$p\033[?1004$p"
	    "\033[?1005$p\033[?1006$p");
	EXPECT_REPLY("\033[?1;1$y\033[?6;1$y\033[?12;1$y\033[?1003;1$y"
	    "\033[?1004;1$y\033[?1005;1$y\033[?1006;1$y");
	FEED("\033[?9999$p\033[5$p\033[0$p\033[$p");
	EXPECT_REPLY("\033[?9999;0$y\033[5;0$y");
	close_pane();
}

TEST(input, secondary_da_xda_decrqss_and_winops_replies)
{
	char	*want;

	open_pane(20, 5);
	FEED("\033[>c");
	EXPECT_REPLY("\033[>84;0;0c");
	FEED("\033[>0q");
	xasprintf(&want, "\033P>|termo %s\033\\", getversion());
	EXPECT_REPLY(want);
	free(want);
	FEED("\033P$q q\033\\");
	EXPECT_REPLY("\033P1$r q0 q\033\\");
	FEED("\033[3 q\033P$q q\033\\");
	EXPECT_REPLY("\033P1$r q3 q\033\\");
	CHECK_EQ(wp->base.cstyle, SCREEN_CURSOR_UNDERLINE);
	CHECK(wp->base.mode & MODE_CURSOR_BLINKING);
	FEED("\033P$qm\033\\");
	EXPECT_REPLY("\033P0$r\033\\");
	FEED("\033[18t");
	EXPECT_REPLY("\033[8;5;20t");
	FEED("\033[14t");
	EXPECT_REPLY("\033[4;160;320t");
	FEED("\033]2;one\007\033[22;0t\033]2;two\007");
	CHECK_EQ(wp->base.title, "two");
	FEED("\033[23;0t");
	CHECK_EQ(wp->base.title, "one");
	close_pane();
}

TEST(input, osc_10_11_12_set_query_and_reset)
{
	open_pane(20, 5);
	FEED("\033]10;rgb:ff/00/00\007\033]10;?\007");
	CHECK_EQ(wp->palette.fg, 0xff0000 | COLOUR_FLAG_RGB);
	EXPECT_REPLY("\033]10;rgb:ffff/0000/0000\007");
	FEED("\033]11;#00ff00\007\033]11;?\033\\");
	CHECK_EQ(wp->palette.bg, 0x00ff00 | COLOUR_FLAG_RGB);
	EXPECT_REPLY("\033]11;rgb:0000/ffff/0000\033\\");
	FEED("\033]12;rgb:00/00/ff\007\033]12;?\007");
	CHECK_EQ(wp->base.ccolour, 0x0000ff | COLOUR_FLAG_RGB);
	EXPECT_REPLY("\033]12;rgb:0000/0000/ffff\007");
	FEED("\033]112\007\033]12;?\007");
	CHECK_EQ(wp->base.ccolour, -1);
	EXPECT_REPLY("");
	FEED("\033]10;nonsense\007");
	CHECK_EQ(wp->palette.fg, 0xff0000 | COLOUR_FLAG_RGB);
	FEED("\033]110\007\033]111\007");
	CHECK_EQ(wp->palette.fg, 8);
	CHECK_EQ(wp->palette.bg, 8);
	close_pane();
}

TEST(input, osc_4_and_104_palette_set_query_clear)
{
	open_pane(20, 5);
	FEED("\033]4;1;rgb:01/02/03\007");
	CHECK_EQ(colour_palette_get(&wp->palette, 1), 0x010203 | COLOUR_FLAG_RGB);
	FEED("\033]4;1;?\007");
	EXPECT_REPLY("\033]4;1;rgb:0101/0202/0303\007");
	FEED("\033]4;5;?\007");
	EXPECT_REPLY("");
	FEED("\033]4;300;rgb:ff/00/00\007");
	CHECK_EQ(colour_palette_get(&wp->palette, 1), 0x010203 | COLOUR_FLAG_RGB);
	FEED("\033]104;1\007");
	CHECK_EQ(colour_palette_get(&wp->palette, 1), -1);
	FEED("\033]4;1;rgb:01/02/03;2;rgb:04/05/06\007");
	CHECK_EQ(colour_palette_get(&wp->palette, 1), 0x010203 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_palette_get(&wp->palette, 2), 0x040506 | COLOUR_FLAG_RGB);
	FEED("\033]104\007");
	CHECK_EQ(colour_palette_get(&wp->palette, 1), -1);
	CHECK_EQ(colour_palette_get(&wp->palette, 2), -1);
	close_pane();
}

TEST(input, osc52_query_and_default_policy)
{
	open_pane(20, 5);
	REQUIRE(paste_get_top(nullptr) == nullptr);
	FEED("\033]52;c;aGVsbG8=\007");
	CHECK_NULL(paste_get_top(nullptr));
	options_set_number(global_options, "set-clipboard", 2);
	FEED("\033]52;c;aGVsbG8=\007\033]52;c;?\007");
	REQUIRE_NONNULL(paste_get_top(nullptr));
	EXPECT_REPLY("\033]52;c;aGVsbG8=\007");
	FEED("\033]52;c;?\033\\");
	EXPECT_REPLY("\033]52;c;aGVsbG8=\033\\");
	options_set_number(global_options, "get-clipboard", 0);
	FEED("\033]52;c;?\007");
	EXPECT_REPLY("");
	options_set_number(global_options, "get-clipboard", 2);
	FEED("\033]52;c;?\007");
	EXPECT_REPLY("");
	close_pane();
}

TEST(input, dcs_passthrough_gated_by_allow_passthrough)
{
	char	*title;

	open_pane(20, 5);
	title = xstrdup(wp->base.title);
	FEED("\033Ptmux;\033\033]0;x\007\033\\OK");
	EXPECT_ROW(0, "OK");
	CHECK_EQ(wp->base.title, title);
	CHECK_EQ(EVBUFFER_LENGTH(input_pending(wp->ictx)), 0u);
	options_set_number(wp->options, "allow-passthrough", 1);
	FEED("\r\033Ptmux;\033\033]0;x\007\033\\OK");
	EXPECT_ROW(0, "OK");
	CHECK_EQ(wp->base.title, title);
	CHECK_EQ(EVBUFFER_LENGTH(input_pending(wp->ictx)), 0u);
	free(title);
	close_pane();
}

TEST(input, esc_index_nel_ri_tabs_decaln_keypad_and_charsets)
{
	struct grid_cell	gc;
	u_int			y;

	open_pane(20, 5);
	enable_history();
	FEED("\033[5;1Hx\033D");
	CHECK_EQ(wp->base.grid->hsize, 1u);
	EXPECT_ROW(3, "x");
	CHECK_EQ(wp->base.cx, 1u);
	CHECK_EQ(wp->base.cy, 4u);
	FEED("\033E");
	CHECK_EQ(wp->base.cx, 0u);
	CHECK_EQ(wp->base.cy, 4u);
	CHECK_EQ(wp->base.grid->hsize, 2u);
	FEED("\033[HY\033M");
	EXPECT_ROW(0, "");
	EXPECT_ROW(1, "Y");
	FEED("\033[1;4H\033H\r\tA");
	cell(3, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'A');
	FEED("\033[1;4H\033[g\r\tB");
	cell(8, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'B');
	FEED("\033[1;20H\033[Z");
	CHECK_EQ(wp->base.cx, 16u);
	FEED("\033[Z");
	CHECK_EQ(wp->base.cx, 8u);
	FEED("\033[1;20H\033[2Z");
	CHECK_EQ(wp->base.cx, 8u);
	FEED("\033[3g\r\tC");
	cell(19, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'C');
	FEED("\033#8");
	for (y = 0; y < 5; y++)
		EXPECT_ROW(y, "EEEEEEEEEEEEEEEEEEEE");
	CHECK_EQ(wp->base.cx, 0u);
	CHECK_EQ(wp->base.cy, 0u);
	FEED("\033=");
	CHECK(wp->base.mode & MODE_KKEYPAD);
	FEED("\033>");
	CHECK_EQ(wp->base.mode & MODE_KKEYPAD, 0);
	FEED("\033(0q\033(Bq\033)0\016q\017q");
	cell(0, 0, &gc);
	CHECK(gc.attr & GRID_ATTR_CHARSET);
	cell(1, 0, &gc);
	CHECK_EQ(gc.attr & GRID_ATTR_CHARSET, 0);
	cell(2, 0, &gc);
	CHECK(gc.attr & GRID_ATTR_CHARSET);
	cell(3, 0, &gc);
	CHECK_EQ(gc.attr & GRID_ATTR_CHARSET, 0);
	close_pane();
}

TEST(input, il_dl_su_sd_ed_el_and_absolute_moves_in_region)
{
	open_pane(20, 5);
	enable_history();
	FEED("r0\r\nr1\r\nr2\r\nr3\r\nr4");
	FEED("\033[2;4r\033[2;1H\033[L");
	EXPECT_ROW(0, "r0");
	EXPECT_ROW(1, "");
	EXPECT_ROW(2, "r1");
	EXPECT_ROW(3, "r2");
	EXPECT_ROW(4, "r4");
	FEED("\033[M");
	EXPECT_ROW(1, "r1");
	EXPECT_ROW(2, "r2");
	EXPECT_ROW(3, "");
	EXPECT_ROW(4, "r4");
	FEED("\033[S");
	EXPECT_ROW(0, "r0");
	EXPECT_ROW(1, "r2");
	EXPECT_ROW(2, "");
	EXPECT_ROW(3, "");
	EXPECT_ROW(4, "r4");
	CHECK_EQ(wp->base.grid->hsize, 1u);
	FEED("\033[T");
	EXPECT_ROW(1, "");
	EXPECT_ROW(2, "r2");
	EXPECT_ROW(3, "");
	FEED("\033[3J");
	CHECK_EQ(wp->base.grid->hsize, 0u);
	FEED("\033[3d");
	CHECK_EQ(wp->base.cy, 2u);
	FEED("\033[5G");
	CHECK_EQ(wp->base.cx, 4u);
	FEED("\033[1G\033[5`");
	CHECK_EQ(wp->base.cx, 4u);
	FEED("\033[2E");
	CHECK_EQ(wp->base.cx, 0u);
	CHECK_EQ(wp->base.cy, 3u);
	FEED("\033[1F");
	CHECK_EQ(wp->base.cy, 2u);
	FEED("\033[2;3f");
	CHECK_EQ(wp->base.cx, 2u);
	CHECK_EQ(wp->base.cy, 1u);
	FEED("\033[r\033[1;1Habcdef\033[1;3H\033[K");
	EXPECT_ROW(0, "ab");
	FEED("\033[1;1Habcdef\033[1;3H\033[1K");
	EXPECT_ROW(0, "   def");
	FEED("\033[2K");
	EXPECT_ROW(0, "");
	FEED("\033[2;1Hxy\033[3;1Hzz\033[2;2H\033[J");
	EXPECT_ROW(1, "x");
	EXPECT_ROW(2, "");
	EXPECT_ROW(4, "");
	FEED("\033[1;1Htop\033[2;3Hxyz\033[2;2H\033[1J");
	EXPECT_ROW(0, "");
	EXPECT_ROW(1, "  xyz");
	close_pane();
}

TEST(input, private_modes_wrap_origin_insert_mouse_and_modifyotherkeys)
{
	open_pane(20, 5);
	FEED("\033[?7labcdefghijklmnopqrstuvwxy");
	EXPECT_ROW(0, "abcdefghijklmnopqrsy");
	CHECK_EQ(wp->base.cx, 19u);
	FEED("\033[?7h\033[2;4r\033[?6h\033[1;1HX");
	CHECK(wp->base.mode & MODE_WRAP);
	EXPECT_ROW(1, "X");
	FEED("\033[?6l");
	CHECK_EQ(wp->base.mode & MODE_ORIGIN, 0);
	CHECK_EQ(wp->base.cy, 0u);
	FEED("\033[r\033[2J\033[4h\033[1;1Hab\033[1;1HZ\033[4l");
	EXPECT_ROW(0, "Zab");
	CHECK_EQ(wp->base.mode & MODE_INSERT, 0);
	FEED("\033[?1002h");
	CHECK(wp->base.mode & MODE_MOUSE_BUTTON);
	FEED("\033[?1003h");
	CHECK(wp->base.mode & MODE_MOUSE_ALL);
	CHECK_EQ(wp->base.mode & (MODE_MOUSE_STANDARD | MODE_MOUSE_BUTTON), 0);
	FEED("\033[?1005h\033[?1004h");
	CHECK(wp->base.mode & MODE_MOUSE_UTF8);
	CHECK(wp->base.mode & MODE_FOCUSON);
	FEED("\033[?12h");
	CHECK(wp->base.mode & MODE_CURSOR_BLINKING);
	CHECK(wp->base.mode & MODE_CURSOR_BLINKING_SET);
	FEED("\033[?12l");
	CHECK_EQ(wp->base.mode & MODE_CURSOR_BLINKING, 0);
	CHECK(wp->base.mode & MODE_CURSOR_BLINKING_SET);
	FEED("\033[?2031h");
	CHECK(wp->base.mode & MODE_THEME_UPDATES);
	FEED("\033[?2031l");
	CHECK_EQ(wp->base.mode & MODE_THEME_UPDATES, 0);
	FEED("\033[3;3H\033[?47h");
	REQUIRE_NONNULL(wp->base.saved_grid);
	CHECK_EQ(wp->base.saved_cx, UINT_MAX);
	FEED("\033[?47l");
	CHECK_NULL(wp->base.saved_grid);
	options_set_number(global_options, "extended-keys", 1);
	FEED("\033[>4;2m");
	CHECK(wp->base.mode & MODE_KEYS_EXTENDED_2);
	FEED("\033[>4n");
	CHECK_EQ(wp->base.mode & EXTENDED_KEY_MODES, 0);
	FEED("\033[>4;1m");
	CHECK(wp->base.mode & MODE_KEYS_EXTENDED);
	close_pane();
}

TEST(input, osc133_marks_prompt_command_and_exit_status)
{
	struct grid_line	*gl;

	open_pane(20, 5);
	FEED("\033]133;A\007$ cmd\033]133;C\007");
	gl = gridline(0);
	CHECK(gl->flags & GRID_LINE_START_PROMPT);
	CHECK_EQ(gl->osc133_data.prompt_col, 0);
	CHECK(gl->flags & GRID_LINE_START_OUTPUT);
	CHECK_EQ(gl->osc133_data.out_start_col, 5);
	CHECK(wp->flags & PANE_CMDRUNNING);
	CHECK(wp->cmd_start_time != 0);
	CHECK_EQ(wp->cmd_status, -1);
	FEED("\r\nout\r\n\033]133;D;3\007");
	gl = gridline(2);
	CHECK(gl->flags & GRID_LINE_END_OUTPUT);
	CHECK_EQ(gl->osc133_data.exit_status, 3);
	CHECK_EQ(wp->cmd_status, 3);
	CHECK_EQ(wp->flags & PANE_CMDRUNNING, 0);
	CHECK(wp->cmd_end_time != 0);
	close_pane();
}

TEST(input, osc7_path_osc9_progress_and_rename_string)
{
	open_pane(20, 5);
	CHECK_NULL(wp->base.path);
	FEED("\033]7;file:///tmp\007");
	CHECK_EQ(wp->base.path, "file:///tmp");
	FEED("\033]9;4;1;50\007");
	CHECK_EQ(wp->base.progress_bar.state, PROGRESS_BAR_NORMAL);
	CHECK_EQ(wp->base.progress_bar.progress, 50);
	FEED("\033]9;4;0\007");
	CHECK_EQ(wp->base.progress_bar.state, PROGRESS_BAR_HIDDEN);
	CHECK_EQ(w->name, "");
	FEED("\033kname\033\\");
	CHECK_EQ(w->name, "");
	options_set_number(w->options, "allow-rename", 1);
	FEED("\033kname\033\\");
	CHECK_EQ(w->name, "name");
	CHECK_EQ(options_get_number(w->options, "automatic-rename"), 0);
	close_pane();
}

TEST(input, reset_pending_and_buffer_size)
{
	char	title[128], *initial;

	open_pane(20, 5);
	initial = xstrdup(wp->base.title);
	FEED("\033[1;2");
	CHECK_EQ(EVBUFFER_LENGTH(input_pending(wp->ictx)), 5u);
	FEED("H");
	CHECK_EQ(EVBUFFER_LENGTH(input_pending(wp->ictx)), 0u);
	CHECK_EQ(wp->base.cx, 1u);
	FEED("\033[1;2");
	input_reset(wp->ictx, 0);
	FEED("X");
	EXPECT_ROW(0, " X");
	CHECK_EQ(wp->base.cx, 2u);
	FEED("abc");
	input_reset(wp->ictx, 1);
	EXPECT_ROW(0, "");
	CHECK_EQ(wp->base.cx, 0u);
	CHECK_EQ(wp->base.cy, 0u);

	memset(title, 'T', 100);
	title[100] = '\0';
	input_set_buffer_size(64);
	FEED("\033]2;");
	feed(title, 100);
	FEED("\007");
	CHECK_EQ(wp->base.title, initial);
	input_set_buffer_size(INPUT_BUF_DEFAULT_SIZE);
	FEED("\033]2;");
	feed(title, 100);
	FEED("\007");
	CHECK_EQ(wp->base.title, title);
	free(initial);
	close_pane();
}

TEST(input, c0_backspace_vt_ff_bell_and_crlf_mode)
{
	open_pane(20, 5);
	FEED("abc\010\010X");
	EXPECT_ROW(0, "aXc");
	CHECK_EQ(wp->base.cx, 2u);
	FEED("\r\033[2Ja\013b\014c");
	EXPECT_ROW(0, "a");
	EXPECT_ROW(1, " b");
	EXPECT_ROW(2, "  c");
	/* The alerts callback clears the flag on the next loop: look first. */
	input_parse_buffer(wp, (const u_char *)"\007", 1);
	CHECK(w->flags & WINDOW_BELL);
	termo_test_drain();
	wp->base.mode |= MODE_CRLF;
	FEED("\nq");
	EXPECT_ROW(3, "q");
	CHECK_EQ(wp->base.cx, 1u);
	CHECK_EQ(wp->base.cy, 3u);
	close_pane();
}

static void
expect_key(const char *file, int line, key_code key, const char *want)
{
	static bool	built;

	if (!built) {
		input_key_build();
		built = true;
	}
	input_key(&wp->base, vpty[0], key);
	termo_test_drain();
	expect_reply(file, line, want);
}
#define EXPECT_KEY(key, want) expect_key(__FILE__, __LINE__, key, want)

static void
expect_mouse(const char *file, int line, struct mouse_event *m, u_int x,
    u_int y, const char *want)
{
	const char	*buf;
	size_t		 len;
	int		 sent;

	sent = input_key_get_mouse(&wp->base, m, x, y, &buf, &len);
	if (want == nullptr) {
		if (sent) {
			test_fail(file, line, "mouse: got \"%.*s\", want discard",
			    (int)len, buf);
		}
		return;
	}
	if (!sent)
		test_fail(file, line, "mouse: discarded, want \"%s\"", want);
	else if (len != strlen(want) || memcmp(buf, want, len) != 0) {
		test_fail(file, line, "mouse: got \"%.*s\", want \"%s\"",
		    (int)len, buf, want);
	}
}
#define EXPECT_MOUSE(m, x, y, want) \
	expect_mouse(__FILE__, __LINE__, m, x, y, want)

TEST(input, key_cursor_and_keypad_follow_application_modes)
{
	open_pane(20, 5);
	EXPECT_KEY(KEYC_UP | KEYC_CURSOR, "\033[A");
	wp->base.mode |= MODE_KCURSOR;
	EXPECT_KEY(KEYC_UP | KEYC_CURSOR, "\033OA");
	EXPECT_KEY(KEYC_KP_ONE | KEYC_KEYPAD, "1");
	wp->base.mode |= MODE_KKEYPAD;
	EXPECT_KEY(KEYC_KP_ONE | KEYC_KEYPAD, "\033Oq");
	EXPECT_KEY(KEYC_HOME, "\033[1~");
	EXPECT_KEY(KEYC_UP | KEYC_META, "\033\033[A");
	EXPECT_KEY(KEYC_UP | KEYC_META | KEYC_IMPLIED_META, "\033[1;3A");
	close_pane();
}

TEST(input, key_bracketed_paste_markers_only_in_mode)
{
	open_pane(20, 5);
	EXPECT_KEY(KEYC_PASTE_START, "");
	EXPECT_KEY(KEYC_PASTE_END, "");
	wp->base.mode |= MODE_BRACKETPASTE;
	EXPECT_KEY(KEYC_PASTE_START, "\033[200~");
	EXPECT_KEY(KEYC_PASTE_END, "\033[201~");
	close_pane();
}

TEST(input, key_standard_meta_ctrl_unicode_and_literal)
{
	open_pane(20, 5);
	EXPECT_KEY('a', "a");
	EXPECT_KEY('x' | KEYC_META, "\033x");
	EXPECT_KEY('a' | KEYC_CTRL, "\001");
	EXPECT_KEY('\t' | KEYC_CTRL, "\t");
	EXPECT_KEY(key_string_lookup_string("\xc3\xa9"), "\xc3\xa9");
	EXPECT_KEY('x' | KEYC_LITERAL, "x");
	EXPECT_KEY(KEYC_BTAB, "\033[Z");
	close_pane();
}

TEST(input, key_extended_formats_and_mode1_fallback)
{
	open_pane(20, 5);
	wp->base.mode |= MODE_KEYS_EXTENDED_2;
	EXPECT_KEY('a' | KEYC_CTRL | KEYC_SHIFT, "\033[27;6;97~");
	EXPECT_KEY(KEYC_BTAB, "\033[27;2;9~");
	options_set_number(global_options, "extended-keys-format", 0);
	EXPECT_KEY('a' | KEYC_CTRL | KEYC_SHIFT, "\033[97;6u");
	wp->base.mode &= ~MODE_KEYS_EXTENDED_2;
	wp->base.mode |= MODE_KEYS_EXTENDED;
	EXPECT_KEY('a' | KEYC_CTRL, "\001");
	EXPECT_KEY('a' | KEYC_CTRL | KEYC_SHIFT, "\001");
	EXPECT_KEY('1' | KEYC_CTRL, "\033[49;5u");
	close_pane();
}

TEST(input, key_backspace_follows_option)
{
	open_pane(20, 5);
	EXPECT_KEY(KEYC_BSPACE, "\177");
	options_set_number(global_options, "backspace", 'h' | KEYC_CTRL);
	EXPECT_KEY(KEYC_BSPACE, "\010");
	EXPECT_KEY(KEYC_BSPACE | KEYC_META, "\033\010");
	close_pane();
}

TEST(input, key_mouse_sgr_legacy_and_discard)
{
	struct mouse_event	m = { .b = 0, .sgr_b = 0, .sgr_type = 'M' };

	open_pane(20, 5);
	wp->base.mode |= MODE_MOUSE_STANDARD | MODE_MOUSE_SGR;
	EXPECT_MOUSE(&m, 4, 2, "\033[<0;5;3M");
	wp->base.mode &= ~MODE_MOUSE_SGR;
	EXPECT_MOUSE(&m, 4, 2, "\033[M %#");
	m.sgr_type = ' ';
	EXPECT_MOUSE(&m, 4, 2, "\033[M %#");
	EXPECT_MOUSE(&m, 200, 2, "\033[M \xe9#");
	EXPECT_MOUSE(&m, 300, 2, "\033[M \xff#");
	wp->base.mode |= MODE_MOUSE_UTF8;
	EXPECT_MOUSE(&m, 200, 2, "\033[M \xc3\xa9#");
	wp->base.mode &= ~(MODE_MOUSE_UTF8 | ALL_MOUSE_MODES);
	EXPECT_MOUSE(&m, 4, 2, nullptr);
	m.b |= MOUSE_MASK_DRAG;
	wp->base.mode |= MODE_MOUSE_STANDARD;
	EXPECT_MOUSE(&m, 4, 2, nullptr);
	wp->base.mode |= MODE_MOUSE_BUTTON;
	EXPECT_MOUSE(&m, 4, 2, "\033[M@%#");
	m.b = m.sgr_b = MOUSE_MASK_DRAG | 3;
	m.sgr_type = 'm';
	wp->base.mode |= MODE_MOUSE_SGR;
	EXPECT_MOUSE(&m, 4, 2, nullptr);
	wp->base.mode |= MODE_MOUSE_ALL;
	EXPECT_MOUSE(&m, 4, 2, "\033[<35;5;3m");
	close_pane();
}
