#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
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
