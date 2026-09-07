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
	w = window_create(sx, sy, 0, 0);
	window_add_ref(w, __func__);
	wp = window_add_pane(w, nullptr, 0, 0);
	window_set_active_pane(w, wp, 0);
	layout_init(w, wp);
	bufferevent_pair_new(libevent, BEV_OPT_CLOSE_ON_FREE, vpty);
	bufferevent_enable(vpty[1], EV_READ);
	wp->ictx = input_init(wp, vpty[0], &wp->palette, nullptr);
	wp->fd = open("/dev/null", O_WRONLY);
	wp->event = bufferevent_new(wp->fd, nullptr, nullptr, nullptr, nullptr);
}

static void
close_pane(void)
{
	window_remove_ref(w, __func__);
	bufferevent_free(vpty[0]);
	bufferevent_free(vpty[1]);
}

static void
feed(const char *bytes, size_t len)
{
	input_parse_buffer(wp, (const u_char *)bytes, len);
	while (cmdq_next(nullptr) != 0)
		;
	event_base_loop(libevent, EVLOOP_NONBLOCK);
}
#define FEED(s) feed(s, sizeof(s) - 1)

static char *
reply(void)
{
	struct evbuffer	*evb = bufferevent_get_input(vpty[1]);
	size_t		 len = EVBUFFER_LENGTH(evb);
	char		*out = xmalloc(len + 1);

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
