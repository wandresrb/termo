#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"
#include "test.h"

static struct screen		s;
static struct screen_write_ctx	ctx;

static void
open_screen(u_int sx, u_int sy)
{
	screen_init(&s, sx, sy, 50);
	screen_write_start(&ctx, &s);
}

static void
close_screen(void)
{
	screen_write_stop(&ctx);
	screen_free(&s);
}

static void
text(const char *str)
{
	screen_write_puts(&ctx, &grid_default_cell, "%s", str);
}

static void
expect_view(const char *file, int line, u_int y, const char *want)
{
	struct grid	*gd = s.grid;
	char		*got = grid_string_cells(gd, 0, gd->hsize + y, gd->sx,
	    nullptr, GRID_STRING_TRIM_SPACES, nullptr);

	if (strcmp(got, want) != 0)
		test_fail(file, line, "row %u: got \"%s\", want \"%s\"", y, got, want);
	free(got);
}
#define EXPECT_VIEW(y, want) expect_view(__FILE__, __LINE__, y, want)

static void
expect_history(const char *file, int line, u_int y, const char *want)
{
	char	*got = grid_string_cells(s.grid, 0, y, s.grid->sx, nullptr,
	    GRID_STRING_TRIM_SPACES, nullptr);

	if (strcmp(got, want) != 0)
		test_fail(file, line, "history %u: got \"%s\", want \"%s\"", y, got, want);
	free(got);
}
#define EXPECT_HISTORY(y, want) expect_history(__FILE__, __LINE__, y, want)

TEST(screen_write, puts_advances_cursor)
{
	open_screen(10, 3);
	text("abc");
	EXPECT_VIEW(0, "abc");
	CHECK_EQ(s.cx, 3u);
	CHECK_EQ(s.cy, 0u);
	close_screen();
}

TEST(screen_write, wraps_at_last_column)
{
	open_screen(5, 3);
	text("abcdefg");
	EXPECT_VIEW(0, "abcde");
	EXPECT_VIEW(1, "fg");
	CHECK(grid_get_line(s.grid, 0)->flags & GRID_LINE_WRAPPED);
	CHECK_EQ(s.cx, 2u);
	CHECK_EQ(s.cy, 1u);
	close_screen();
}

TEST(screen_write, wide_character_that_does_not_fit_wraps)
{
	struct grid_cell	gc;

	open_screen(5, 3);
	text("abcd\xe4\xb8\xad");
	EXPECT_VIEW(0, "abcd");
	EXPECT_VIEW(1, "\xe4\xb8\xad");
	grid_view_get_cell(s.grid, 0, 1, &gc);
	CHECK_EQ(gc.data.width, 2);
	grid_view_get_cell(s.grid, 1, 1, &gc);
	CHECK(gc.flags & GRID_FLAG_PADDING);
	CHECK_EQ(s.cx, 2u);
	CHECK_EQ(s.cy, 1u);
	close_screen();
}

TEST(screen_write, cursormove_clamps_and_keeps_axes)
{
	open_screen(10, 3);
	screen_write_cursormove(&ctx, 99, 99, 0);
	CHECK_EQ(s.cx, 9u);
	CHECK_EQ(s.cy, 2u);
	screen_write_cursormove(&ctx, -1, 1, 0);
	CHECK_EQ(s.cx, 9u);
	CHECK_EQ(s.cy, 1u);
	screen_write_cursormove(&ctx, 4, -1, 0);
	CHECK_EQ(s.cx, 4u);
	CHECK_EQ(s.cy, 1u);
	close_screen();
}

TEST(screen_write, insert_and_delete_characters)
{
	open_screen(10, 2);
	text("abcdef");
	screen_write_cursormove(&ctx, 2, 0, 0);
	screen_write_insertcharacter(&ctx, 2, 8);
	EXPECT_VIEW(0, "ab  cdef");
	screen_write_deletecharacter(&ctx, 3, 8);
	EXPECT_VIEW(0, "abdef");
	screen_write_clearendofline(&ctx, 8);
	EXPECT_VIEW(0, "ab");
	close_screen();
}

TEST(screen_write, insert_and_delete_lines)
{
	open_screen(10, 3);
	text("one");
	screen_write_cursormove(&ctx, 0, 1, 0);
	text("two");
	screen_write_cursormove(&ctx, 0, 2, 0);
	text("three");
	screen_write_cursormove(&ctx, 0, 1, 0);
	screen_write_insertline(&ctx, 1, 8);
	EXPECT_VIEW(0, "one");
	EXPECT_VIEW(1, "");
	EXPECT_VIEW(2, "two");
	screen_write_deleteline(&ctx, 1, 8);
	EXPECT_VIEW(1, "two");
	EXPECT_VIEW(2, "");
	close_screen();
}

TEST(screen_write, linefeed_at_bottom_scrolls_into_history)
{
	open_screen(10, 3);
	text("first");
	screen_write_cursormove(&ctx, 0, 2, 0);
	text("last");
	screen_write_linefeed(&ctx, 0, 8);
	CHECK_EQ(s.grid->hsize, 1u);
	EXPECT_HISTORY(0, "first");
	EXPECT_VIEW(1, "last");
	EXPECT_VIEW(2, "");
	CHECK_EQ(s.cy, 2u);
	close_screen();
}

TEST(screen_write, scroll_region_keeps_lines_outside_it)
{
	open_screen(10, 3);
	text("top");
	screen_write_cursormove(&ctx, 0, 1, 0);
	text("mid");
	screen_write_cursormove(&ctx, 0, 2, 0);
	text("bottom");
	screen_write_scrollregion(&ctx, 0, 1);
	CHECK_EQ(s.rupper, 0u);
	CHECK_EQ(s.rlower, 1u);
	screen_write_cursormove(&ctx, 0, 1, 0);
	screen_write_linefeed(&ctx, 0, 8);
	EXPECT_VIEW(0, "mid");
	EXPECT_VIEW(1, "");
	EXPECT_VIEW(2, "bottom");
	/* A region that starts at row 0 still feeds the scrollback. */
	CHECK_EQ(s.grid->hsize, 1u);
	EXPECT_HISTORY(0, "top");
	close_screen();
}

TEST(screen_write, reverse_index_at_top_scrolls_down)
{
	open_screen(10, 3);
	text("top");
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_reverseindex(&ctx, 8);
	EXPECT_VIEW(0, "");
	EXPECT_VIEW(1, "top");
	CHECK_EQ(s.cy, 0u);
	close_screen();
}

TEST(screen_write, clear_screen_and_line)
{
	open_screen(10, 3);
	text("abc");
	screen_write_cursormove(&ctx, 0, 1, 0);
	text("def");
	screen_write_clearline(&ctx, 8);
	EXPECT_VIEW(0, "abc");
	EXPECT_VIEW(1, "");
	screen_write_clearscreen(&ctx, 8);
	EXPECT_VIEW(0, "");
	close_screen();
}

TEST(screen_write, carriage_return_and_backspace)
{
	open_screen(10, 3);
	text("abc");
	screen_write_carriagereturn(&ctx);
	CHECK_EQ(s.cx, 0u);
	screen_write_cursormove(&ctx, 3, 0, 0);
	screen_write_backspace(&ctx);
	CHECK_EQ(s.cx, 2u);
	screen_write_cursormove(&ctx, 0, 1, 0);
	screen_write_backspace(&ctx);
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 1u);
	close_screen();
}

TEST(screen_write, alternate_screen_saves_and_restores)
{
	struct grid_cell	gc;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	open_screen(10, 3);
	text("main");
	screen_write_alternateon(&ctx, &gc, 1);
	REQUIRE_NONNULL(s.saved_grid);
	EXPECT_VIEW(0, "");
	/* Like xterm's 1049: the screen is cleared, the cursor stays put. */
	CHECK_EQ(s.cx, 4u);
	text("alt");
	EXPECT_VIEW(0, "    alt");
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_alternateoff(&ctx, &gc, 1);
	CHECK_NULL(s.saved_grid);
	EXPECT_VIEW(0, "main");
	CHECK_EQ(s.cx, 4u);
	close_screen();
}

TEST(screen_write, cell_keeps_attributes)
{
	struct grid_cell	gc, got;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	utf8_set(&gc.data, 'X');
	gc.attr = GRID_ATTR_BRIGHT | GRID_ATTR_UNDERSCORE;
	gc.fg = 1;
	open_screen(10, 2);
	screen_write_cell(&ctx, &gc);
	screen_write_putc(&ctx, &grid_default_cell, 'y');
	grid_view_get_cell(s.grid, 0, 0, &got);
	CHECK_EQ(got.data.data[0], 'X');
	CHECK_EQ(got.attr, GRID_ATTR_BRIGHT | GRID_ATTR_UNDERSCORE);
	CHECK_EQ(got.fg, 1);
	grid_view_get_cell(s.grid, 1, 0, &got);
	CHECK_EQ(got.data.data[0], 'y');
	CHECK_EQ(got.attr, 0);
	close_screen();
}

TEST(screen_write, resize_reflows_long_lines)
{
	open_screen(10, 3);
	text("0123456789");
	screen_write_stop(&ctx);
	screen_resize(&s, 5, 3, 1);
	CHECK_EQ(s.grid->sx, 5u);
	CHECK_EQ(s.grid->hsize + s.grid->sy, 4u);
	EXPECT_HISTORY(0, "01234");
	EXPECT_HISTORY(1, "56789");
	screen_resize(&s, 10, 3, 1);
	CHECK_EQ(s.grid->hsize, 0u);
	EXPECT_VIEW(0, "0123456789");
	screen_free(&s);
}

TEST(screen_write, no_wrap_mode_sticks_at_last_column_and_drops_wide)
{
	struct grid_cell	wide;

	termo_test_wide(&wide, "\xe4\xb8\xad");
	open_screen(5, 2);
	screen_write_mode_clear(&ctx, MODE_WRAP);
	text("abcdef");
	EXPECT_VIEW(0, "abcdf");
	CHECK_EQ(s.cx, 4u);
	CHECK_EQ(s.cy, 0u);
	screen_write_cursormove(&ctx, 4, 0, 0);
	screen_write_cell(&ctx, &wide);
	EXPECT_VIEW(0, "abcdf");
	CHECK_EQ(s.cx, 4u);
	close_screen();
}

TEST(screen_write, overwriting_wide_cell_clears_padding)
{
	struct grid_cell	wide, got;

	termo_test_wide(&wide, "\xe4\xb8\xad");
	open_screen(10, 1);
	screen_write_cell(&ctx, &wide);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_putc(&ctx, &grid_default_cell, 'x');
	EXPECT_VIEW(0, "x");
	grid_view_get_cell(s.grid, 1, 0, &got);
	CHECK(!(got.flags & GRID_FLAG_PADDING));

	screen_write_clearline(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_cell(&ctx, &wide);
	screen_write_cursormove(&ctx, 1, 0, 0);
	screen_write_putc(&ctx, &grid_default_cell, 'y');
	EXPECT_VIEW(0, " y");
	grid_view_get_cell(s.grid, 0, 0, &got);
	CHECK(grid_cells_equal(&got, &grid_default_cell));

	/* DCH and ICH move cells blindly, so an orphan padding cell stays. */
	screen_write_clearline(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	text("a\xe4\xb8\xad" "b");
	screen_write_cursormove(&ctx, 1, 0, 0);
	screen_write_deletecharacter(&ctx, 1, 8);
	EXPECT_VIEW(0, "ab");

	screen_write_clearline(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	text("a\xe4\xb8\xad" "b");
	screen_write_cursormove(&ctx, 2, 0, 0);
	screen_write_insertcharacter(&ctx, 1, 8);
	EXPECT_VIEW(0, "a\xe4\xb8\xad b");
	close_screen();
}

TEST(screen_write, scroll_region_insert_delete_scroll_stay_inside)
{
	u_int	y;

	open_screen(10, 5);
	for (y = 0; y < 5; y++) {
		screen_write_cursormove(&ctx, 0, y, 0);
		screen_write_puts(&ctx, &grid_default_cell, "r%u", y);
	}
	screen_write_scrollregion(&ctx, 1, 3);

	screen_write_cursormove(&ctx, 0, 1, 0);
	screen_write_insertline(&ctx, 1, 8);
	EXPECT_VIEW(0, "r0");
	EXPECT_VIEW(1, "");
	EXPECT_VIEW(2, "r1");
	EXPECT_VIEW(3, "r2");
	EXPECT_VIEW(4, "r4");

	screen_write_deleteline(&ctx, 1, 8);
	EXPECT_VIEW(0, "r0");
	EXPECT_VIEW(1, "r1");
	EXPECT_VIEW(2, "r2");
	EXPECT_VIEW(3, "");
	EXPECT_VIEW(4, "r4");

	screen_write_scrollup(&ctx, 1, 8);
	EXPECT_VIEW(0, "r0");
	EXPECT_VIEW(1, "r2");
	EXPECT_VIEW(2, "");
	EXPECT_VIEW(3, "");
	EXPECT_VIEW(4, "r4");
	CHECK_EQ(s.grid->hsize, 1u);
	EXPECT_HISTORY(0, "r1");

	screen_write_scrolldown(&ctx, 1, 8);
	EXPECT_VIEW(0, "r0");
	EXPECT_VIEW(1, "");
	EXPECT_VIEW(2, "r2");
	EXPECT_VIEW(3, "");
	EXPECT_VIEW(4, "r4");

	screen_write_scrollup(&ctx, 99, 8);
	EXPECT_VIEW(0, "r0");
	EXPECT_VIEW(1, "");
	EXPECT_VIEW(2, "");
	EXPECT_VIEW(3, "");
	EXPECT_VIEW(4, "r4");
	CHECK_EQ(s.grid->hsize, 4u);
	EXPECT_HISTORY(2, "r2");

	screen_write_scrollregion(&ctx, 0, 1);
	screen_write_cursormove(&ctx, 0, 3, 0);
	text("r3");
	screen_write_insertline(&ctx, 1, 8);
	EXPECT_VIEW(3, "");
	EXPECT_VIEW(4, "r3");
	/* grid_view_insert_lines on the last row moves nothing and never clears it. */
	screen_write_cursormove(&ctx, 0, 4, 0);
	screen_write_insertline(&ctx, 1, 8);
	EXPECT_VIEW(4, "r3");

	screen_write_clearhistory(&ctx);
	CHECK_EQ(s.grid->hsize, 0u);
	EXPECT_VIEW(0, "r0");
	EXPECT_VIEW(4, "r3");
	close_screen();
}

TEST(screen_write, cursor_motion_respects_region_and_pending_wrap)
{
	open_screen(10, 5);
	screen_write_scrollregion(&ctx, 1, 3);
	screen_write_cursormove(&ctx, 0, 2, 0);
	screen_write_cursorup(&ctx, 5);
	CHECK_EQ(s.cy, 1u);
	screen_write_cursordown(&ctx, 5);
	CHECK_EQ(s.cy, 3u);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_cursorup(&ctx, 1);
	CHECK_EQ(s.cy, 0u);
	screen_write_cursordown(&ctx, 5);
	CHECK_EQ(s.cy, 3u);
	screen_write_cursormove(&ctx, 0, 4, 0);
	screen_write_cursordown(&ctx, 1);
	CHECK_EQ(s.cy, 4u);
	screen_write_cursorright(&ctx, 99);
	CHECK_EQ(s.cx, 9u);
	screen_write_cursorleft(&ctx, 99);
	CHECK_EQ(s.cx, 0u);
	screen_write_cursorright(&ctx, 0);
	CHECK_EQ(s.cx, 1u);
	close_screen();

	open_screen(5, 3);
	text("abcde");
	CHECK_EQ(s.cx, 5u);
	CHECK_EQ(s.cy, 0u);
	screen_write_cursordown(&ctx, 1);
	CHECK_EQ(s.cx, 4u);
	CHECK_EQ(s.cy, 1u);
	close_screen();
}

TEST(screen_write, clearcharacter_and_clearstartofline)
{
	open_screen(10, 2);
	text("abcdefghij");
	screen_write_cursormove(&ctx, 3, 0, 0);
	screen_write_clearcharacter(&ctx, 2, 8);
	EXPECT_VIEW(0, "abc  fghij");
	screen_write_clearcharacter(&ctx, 99, 8);
	EXPECT_VIEW(0, "abc");
	screen_write_cursormove(&ctx, 0, 0, 0);
	text("abcdefghij");
	screen_write_cursormove(&ctx, 4, 0, 0);
	screen_write_clearstartofline(&ctx, 8);
	EXPECT_VIEW(0, "     fghij");
	screen_write_cursormove(&ctx, 9, 0, 0);
	screen_write_clearstartofline(&ctx, 8);
	EXPECT_VIEW(0, "");
	close_screen();
}

TEST(screen_write, clearendofscreen_and_clearstartofscreen)
{
	u_int	y;

	open_screen(10, 4);
	for (y = 0; y < 4; y++) {
		screen_write_cursormove(&ctx, 0, y, 0);
		text("abcd");
	}
	screen_write_cursormove(&ctx, 2, 1, 0);
	screen_write_clearendofscreen(&ctx, 8);
	EXPECT_VIEW(0, "abcd");
	EXPECT_VIEW(1, "ab");
	EXPECT_VIEW(2, "");
	EXPECT_VIEW(3, "");

	for (y = 0; y < 4; y++) {
		screen_write_cursormove(&ctx, 0, y, 0);
		text("abcd");
	}
	screen_write_cursormove(&ctx, 2, 2, 0);
	screen_write_clearstartofscreen(&ctx, 8);
	EXPECT_VIEW(0, "");
	EXPECT_VIEW(1, "");
	EXPECT_VIEW(2, "   d");
	EXPECT_VIEW(3, "abcd");

	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_clearendofscreen(&ctx, 8);
	CHECK_EQ(s.grid->hsize, 0u);
	EXPECT_VIEW(2, "");
	EXPECT_VIEW(3, "");
	close_screen();
}

TEST(screen_write, alternate_screen_restores_cursor_cell_and_history_flag)
{
	struct grid_cell	 gc, gc2;
	struct grid		*saved;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.fg = 3;
	open_screen(10, 3);
	text("main");
	screen_write_cursormove(&ctx, 2, 1, 0);
	screen_write_alternateon(&ctx, &gc, 1);
	REQUIRE_NONNULL(s.saved_grid);
	CHECK_EQ(s.saved_cx, 2u);
	CHECK_EQ(s.saved_cy, 1u);
	CHECK(!(s.grid->flags & GRID_HISTORY));
	saved = s.saved_grid;
	screen_write_alternateon(&ctx, &gc, 1);
	CHECK(s.saved_grid == saved);

	text("alt");
	screen_write_cursormove(&ctx, 0, 0, 0);
	memcpy(&gc2, &grid_default_cell, sizeof gc2);
	screen_write_alternateoff(&ctx, &gc2, 1);
	CHECK_NULL(s.saved_grid);
	CHECK_EQ(s.cx, 2u);
	CHECK_EQ(s.cy, 1u);
	CHECK_EQ(gc2.fg, 3);
	CHECK(s.grid->flags & GRID_HISTORY);
	EXPECT_VIEW(0, "main");
	EXPECT_VIEW(1, "");

	/* The saved cursor is restored even when not in the alternate screen. */
	screen_write_cursormove(&ctx, 5, 2, 0);
	screen_write_alternateoff(&ctx, &gc2, 1);
	CHECK_EQ(s.cx, 2u);
	CHECK_EQ(s.cy, 1u);
	CHECK_NULL(s.saved_grid);
	EXPECT_VIEW(0, "main");
	close_screen();
}

TEST(screen_write, alternate_screen_survives_resize_while_active)
{
	struct grid_cell	gc;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	open_screen(10, 3);
	text("main");
	screen_write_alternateon(&ctx, &gc, 1);
	screen_write_stop(&ctx);
	screen_resize(&s, 6, 2, 1);
	CHECK_EQ(s.grid->sx, 6u);
	CHECK_EQ(s.saved_grid->sx, 10u);
	CHECK_EQ(s.saved_grid->sy, 3u);
	screen_write_start(&ctx, &s);
	screen_write_alternateoff(&ctx, &gc, 1);
	CHECK_NULL(s.saved_grid);
	CHECK_EQ(s.grid->sx, 6u);
	CHECK_EQ(s.grid->sy, 2u);
	CHECK_EQ(s.grid->hsize, 0u);
	EXPECT_VIEW(0, "main");
	CHECK_EQ(s.cx, 4u);
	CHECK_EQ(s.cy, 0u);
	close_screen();
}

TEST(screen_write, text_wraps_on_spaces_newlines_and_reports_overflow)
{
	const struct grid_cell	*gc = &grid_default_cell;

	open_screen(10, 3);
	CHECK_EQ(screen_write_text(&ctx, 0, 10, 3, 0, gc, "hello world foo"), 1);
	EXPECT_VIEW(0, "hello");
	EXPECT_VIEW(1, "world foo");
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 2u);

	screen_write_clearscreen(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	CHECK_EQ(screen_write_text(&ctx, 0, 10, 1, 0, gc, "hello world"), 0);
	EXPECT_VIEW(0, "hello");

	screen_write_clearscreen(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	CHECK_EQ(screen_write_text(&ctx, 0, 10, 1, 1, gc, "abc"), 1);
	CHECK_EQ(s.cx, 3u);
	CHECK_EQ(s.cy, 0u);

	screen_write_clearscreen(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	CHECK_EQ(screen_write_text(&ctx, 0, 10, 3, 0, gc, "ab\ncd"), 1);
	EXPECT_VIEW(0, "ab");
	EXPECT_VIEW(1, "cd");
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 2u);

	screen_write_clearscreen(&ctx, 8);
	screen_write_cursormove(&ctx, 2, 0, 0);
	CHECK_EQ(screen_write_text(&ctx, 2, 5, 3, 0, gc, "abcdefgh"), 1);
	EXPECT_VIEW(0, "  abcde");
	EXPECT_VIEW(1, "  fgh");
	CHECK_EQ(s.cx, 2u);
	CHECK_EQ(s.cy, 2u);
	close_screen();
}

TEST(screen_write, box_hline_vline_draw_expected_glyphs)
{
	struct grid_cell	gc;
	u_int			x, y;

	open_screen(10, 5);
	screen_write_box(&ctx, 6, 3, BOX_LINES_SIMPLE, nullptr, nullptr);
	EXPECT_VIEW(0, "+----+");
	EXPECT_VIEW(1, "|    |");
	EXPECT_VIEW(2, "+----+");
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 0u);

	screen_write_clearscreen(&ctx, 8);
	screen_write_box(&ctx, 6, 3, BOX_LINES_SINGLE, nullptr, nullptr);
	EXPECT_VIEW(0, "lqqqqk");
	EXPECT_VIEW(1, "x    x");
	EXPECT_VIEW(2, "mqqqqj");
	for (y = 0; y < 3; y++) {
		for (x = 0; x < 6; x++) {
			if (y == 1 && x != 0 && x != 5)
				continue;
			grid_view_get_cell(s.grid, x, y, &gc);
			CHECK(gc.attr & GRID_ATTR_CHARSET);
			CHECK(gc.flags & GRID_FLAG_NOPALETTE);
		}
	}

	screen_write_clearscreen(&ctx, 8);
	screen_write_box(&ctx, 6, 3, BOX_LINES_SIMPLE, nullptr, "Ti");
	EXPECT_VIEW(0, "+-Ti-+");
	EXPECT_VIEW(1, "|    |");
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 0u);

	screen_write_cursormove(&ctx, 0, 4, 0);
	screen_write_hline(&ctx, 6, 1, 1, BOX_LINES_SIMPLE, nullptr);
	EXPECT_VIEW(4, "+----+");
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 4u);
	screen_write_hline(&ctx, 6, 0, 0, BOX_LINES_SIMPLE, nullptr);
	EXPECT_VIEW(4, "------");

	screen_write_cursormove(&ctx, 8, 0, 0);
	screen_write_vline(&ctx, 3, 1, 1, nullptr);
	grid_view_get_cell(s.grid, 8, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'w');
	CHECK(gc.attr & GRID_ATTR_CHARSET);
	grid_view_get_cell(s.grid, 8, 1, &gc);
	CHECK_EQ(gc.data.data[0], 'x');
	CHECK(gc.attr & GRID_ATTR_CHARSET);
	grid_view_get_cell(s.grid, 8, 2, &gc);
	CHECK_EQ(gc.data.data[0], 'v');
	CHECK(gc.attr & GRID_ATTR_CHARSET);
	CHECK_EQ(s.cx, 8u);
	CHECK_EQ(s.cy, 0u);
	close_screen();
}

TEST(screen_write, fast_copy_and_preview_copy_cells)
{
	struct screen		src;
	struct screen_write_ctx	sctx;
	struct grid_cell	gc;

	screen_init(&src, 6, 2, 50);
	screen_write_start(&sctx, &src);
	screen_write_puts(&sctx, &grid_default_cell, "abcdef");
	screen_write_cursormove(&sctx, 0, 1, 0);
	screen_write_puts(&sctx, &grid_default_cell, "ghijkl");
	screen_write_cursormove(&sctx, 2, 1, 0);
	screen_write_stop(&sctx);

	open_screen(10, 3);
	screen_write_cursormove(&ctx, 1, 1, 0);
	screen_write_fast_copy(&ctx, &src, 1, src.grid->hsize, 3, 2);
	EXPECT_VIEW(0, "");
	EXPECT_VIEW(1, " bcd");
	EXPECT_VIEW(2, " hij");
	CHECK_EQ(s.cx, 1u);
	CHECK_EQ(s.cy, 1u);

	screen_write_clearscreen(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_preview(&ctx, &src, 4, 2);
	EXPECT_VIEW(0, "bcde");
	EXPECT_VIEW(1, "hijk");
	grid_view_get_cell(s.grid, 1, 1, &gc);
	CHECK_EQ(gc.data.data[0], 'i');
	CHECK(gc.attr & GRID_ATTR_REVERSE);
	close_screen();
	screen_free(&src);
}

TEST(screen_write, nputs_truncates_by_width_and_strlen_counts_cells)
{
	struct grid_cell	gc;

	CHECK_EQ(screen_write_strlen("a\xe4\xb8\xad" "b\tc\001"), 6u);

	open_screen(10, 2);
	screen_write_nputs(&ctx, 3, &grid_default_cell, "abcdef");
	EXPECT_VIEW(0, "abc");
	CHECK_EQ(s.cx, 3u);

	screen_write_clearline(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_nputs(&ctx, 4, &grid_default_cell, "ab\xe4\xb8\xadx");
	EXPECT_VIEW(0, "ab\xe4\xb8\xad");
	CHECK_EQ(s.cx, 4u);

	screen_write_clearline(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_nputs(&ctx, 3, &grid_default_cell, "ab\xe4\xb8\xad");
	EXPECT_VIEW(0, "ab");
	CHECK_EQ(s.cx, 3u);

	screen_write_clearline(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_puts(&ctx, &grid_default_cell, "a\001q\001b");
	grid_view_get_cell(s.grid, 0, 0, &gc);
	CHECK(!(gc.attr & GRID_ATTR_CHARSET));
	grid_view_get_cell(s.grid, 1, 0, &gc);
	CHECK_EQ(gc.data.data[0], 'q');
	CHECK(gc.attr & GRID_ATTR_CHARSET);
	grid_view_get_cell(s.grid, 2, 0, &gc);
	CHECK(!(gc.attr & GRID_ATTR_CHARSET));

	screen_write_clearscreen(&ctx, 8);
	screen_write_cursormove(&ctx, 0, 0, 0);
	screen_write_puts(&ctx, &grid_default_cell, "x\ny");
	EXPECT_VIEW(0, "x");
	EXPECT_VIEW(1, "y");
	close_screen();
}

TEST(screen_write, alignmenttest_fills_e_and_resets_region)
{
	open_screen(5, 3);
	screen_write_scrollregion(&ctx, 0, 1);
	screen_write_cursormove(&ctx, 2, 2, 0);
	screen_write_alignmenttest(&ctx);
	EXPECT_VIEW(0, "EEEEE");
	EXPECT_VIEW(1, "EEEEE");
	EXPECT_VIEW(2, "EEEEE");
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 0u);
	CHECK_EQ(s.rupper, 0u);
	CHECK_EQ(s.rlower, 2u);
	close_screen();
}

TEST(screen_write, mode_set_clear_insert_and_origin)
{
	open_screen(10, 5);
	screen_write_mode_set(&ctx, MODE_INSERT|MODE_ORIGIN);
	CHECK(s.mode & MODE_INSERT);
	CHECK(s.mode & MODE_ORIGIN);
	screen_write_mode_clear(&ctx, MODE_INSERT);
	CHECK(!(s.mode & MODE_INSERT));
	CHECK(s.mode & MODE_ORIGIN);
	CHECK_EQ(screen_mode_to_string(MODE_CURSOR|MODE_WRAP), "CURSOR,WRAP");
	CHECK_EQ(screen_mode_to_string(0), "NONE");
	CHECK_EQ(screen_mode_to_string(ALL_MODES), "ALL");

	screen_write_mode_clear(&ctx, MODE_ORIGIN);
	text("ac");
	screen_write_cursormove(&ctx, 1, 0, 0);
	screen_write_mode_set(&ctx, MODE_INSERT);
	screen_write_putc(&ctx, &grid_default_cell, 'b');
	EXPECT_VIEW(0, "abc");
	CHECK_EQ(s.cx, 2u);
	screen_write_mode_clear(&ctx, MODE_INSERT);

	screen_write_scrollregion(&ctx, 1, 3);
	screen_write_mode_set(&ctx, MODE_ORIGIN);
	screen_write_cursormove(&ctx, 0, 0, 1);
	CHECK_EQ(s.cy, 1u);
	screen_write_cursormove(&ctx, 0, 99, 1);
	CHECK_EQ(s.cy, 3u);
	close_screen();
}

TEST(screen_write, screen_reinit_keeps_history_and_title_stack_caps_at_ten)
{
	u_int	i;

	open_screen(10, 3);
	text("first");
	screen_write_cursormove(&ctx, 0, 2, 0);
	screen_write_linefeed(&ctx, 0, 8);
	CHECK_EQ(s.grid->hsize, 1u);
	screen_write_scrollregion(&ctx, 0, 1);
	screen_write_mode_set(&ctx, MODE_CRLF|MODE_INSERT);
	screen_write_cursormove(&ctx, 3, 1, 0);
	screen_reinit(&s, 0);
	CHECK_EQ(s.grid->hsize, 1u);
	EXPECT_HISTORY(0, "first");
	EXPECT_VIEW(0, "");
	EXPECT_VIEW(1, "");
	EXPECT_VIEW(2, "");
	CHECK_EQ(s.rupper, 0u);
	CHECK_EQ(s.rlower, 2u);
	CHECK_EQ(s.mode, MODE_CURSOR|MODE_WRAP|MODE_CRLF);
	CHECK_EQ(s.cx, 0u);
	CHECK_EQ(s.cy, 0u);

	screen_set_title(&s, "one", 0);
	screen_push_title(&s);
	screen_set_title(&s, "two", 0);
	screen_push_title(&s);
	screen_set_title(&s, "three", 0);
	screen_pop_title(&s);
	CHECK_EQ(s.title, "two");
	screen_pop_title(&s);
	CHECK_EQ(s.title, "one");
	screen_pop_title(&s);
	CHECK_EQ(s.title, "one");
	CHECK_EQ(s.ntitles, 0u);
	for (i = 0; i < 12; i++)
		screen_push_title(&s);
	CHECK_EQ(s.ntitles, 10u);
	close_screen();
}

TEST(screen_write, selection_check_emacs_vi_rectangle_and_hide)
{
	struct grid_cell	gc, src, dst;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	open_screen(10, 4);
	screen_set_selection(&s, 2, 0, 5, 0, 0, 0, MODEKEY_EMACS, &gc);
	CHECK_EQ(screen_check_selection(&s, 4, 0), 1);
	CHECK_EQ(screen_check_selection(&s, 5, 0), 0);
	CHECK_EQ(screen_check_selection(&s, 1, 0), 0);
	screen_set_selection(&s, 2, 0, 5, 0, 0, 0, MODEKEY_VI, &gc);
	CHECK_EQ(screen_check_selection(&s, 5, 0), 1);
	CHECK_EQ(screen_check_selection(&s, 6, 0), 0);
	screen_set_selection(&s, 1, 0, 3, 2, 1, 0, MODEKEY_VI, &gc);
	CHECK_EQ(screen_check_selection(&s, 2, 1), 1);
	CHECK_EQ(screen_check_selection(&s, 4, 1), 0);
	CHECK_EQ(screen_check_selection(&s, 2, 3), 0);

	memcpy(&src, &grid_default_cell, sizeof src);
	utf8_set(&src.data, 'z');
	src.fg = 4;
	src.attr = GRID_ATTR_ITALICS;
	CHECK_EQ(screen_select_cell(&s, &dst, &src), 1);
	CHECK_EQ(dst.fg, 4);
	CHECK_EQ(dst.data.data[0], 'z');
	CHECK(dst.attr & GRID_ATTR_ITALICS);

	screen_hide_selection(&s);
	CHECK_EQ(screen_check_selection(&s, 2, 1), 0);
	CHECK_EQ(screen_select_cell(&s, &dst, &src), 0);
	screen_clear_selection(&s);
	CHECK_NULL(s.sel);
	CHECK_EQ(screen_check_selection(&s, 2, 1), 0);
	close_screen();
}
