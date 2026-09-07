#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

static void
put(struct grid *gd, u_int x, u_int y, const char *s)
{
	grid_set_cells(gd, x, y, &grid_default_cell, s, strlen(s));
}

static void
put_wide(struct grid *gd, u_int x, u_int y, const char *s)
{
	struct grid_cell	gc;
	enum utf8_state		st;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	st = utf8_open(&gc.data, *s++);
	while (st == UTF8_MORE)
		st = utf8_append(&gc.data, *s++);
	grid_set_cell(gd, x, y, &gc);
	grid_set_padding(gd, x + 1, y, 0);
}

static void
expect_line(const char *file, int line, struct grid *gd, u_int y,
    const char *want)
{
	char	*got = grid_string_cells(gd, 0, y, gd->sx, nullptr,
	    GRID_STRING_TRIM_SPACES, nullptr);

	if (strcmp(got, want) != 0)
		test_fail(file, line, "line %u: got \"%s\", want \"%s\"", y, got, want);
	free(got);
}
#define EXPECT_LINE(gd, y, want) expect_line(__FILE__, __LINE__, gd, y, want)
#define EXPECT_VIEW(gd, y, want) \
	expect_line(__FILE__, __LINE__, gd, (gd)->hsize + (y), want)

TEST(grid, create_sets_geometry_and_history_flag)
{
	struct grid	*gd = grid_create(10, 3, 100);

	CHECK_EQ(gd->sx, 10u);
	CHECK_EQ(gd->sy, 3u);
	CHECK_EQ(gd->hsize, 0u);
	CHECK_EQ(gd->hlimit, 100u);
	CHECK(gd->flags & GRID_HISTORY);
	grid_destroy(gd);

	gd = grid_create(10, 3, 0);
	CHECK_EQ(gd->flags & GRID_HISTORY, 0);
	grid_destroy(gd);
}

TEST(grid, get_cell_out_of_range_is_default)
{
	struct grid	*gd = grid_create(4, 2, 0);
	struct grid_cell gc;

	grid_get_cell(gd, 0, 0, &gc);
	CHECK(grid_cells_equal(&gc, &grid_default_cell));
	grid_get_cell(gd, 99, 0, &gc);
	CHECK(grid_cells_equal(&gc, &grid_default_cell));
	grid_get_cell(gd, 0, 99, &gc);
	CHECK(grid_cells_equal(&gc, &grid_default_cell));
	grid_destroy(gd);
}

TEST(grid, set_cell_roundtrips_and_ignores_bad_row)
{
	struct grid	*gd = grid_create(4, 2, 0);
	struct grid_cell gc, got;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	utf8_set(&gc.data, 'Q');
	gc.attr = GRID_ATTR_BRIGHT;
	gc.fg = 3;
	gc.bg = 0x1000000 | 200;
	grid_set_cell(gd, 2, 1, &gc);
	grid_get_cell(gd, 2, 1, &got);
	CHECK(grid_cells_equal(&gc, &got));
	CHECK_EQ(got.data.data[0], 'Q');
	CHECK_EQ(got.fg, 3);

	grid_set_cell(gd, 0, 5, &gc);
	grid_get_cell(gd, 0, 5, &got);
	CHECK(grid_cells_equal(&got, &grid_default_cell));
	grid_destroy(gd);
}

TEST(grid, string_cells_reads_back_text)
{
	struct grid	*gd = grid_create(10, 2, 0);
	char		*s;

	put(gd, 0, 0, "hello");
	EXPECT_LINE(gd, 0, "hello");
	s = grid_string_cells(gd, 1, 0, 3, nullptr, 0, nullptr);
	CHECK_EQ(s, "ell");
	free(s);
	s = grid_string_cells(gd, 0, 1, 10, nullptr, 0, nullptr);
	CHECK_EQ(s, "");
	free(s);
	CHECK_EQ(grid_line_length(gd, 0), 5u);
	put(gd, 0, 1, "a b  ");
	CHECK_EQ(grid_line_length(gd, 1), 3u);
	grid_destroy(gd);
}

TEST(grid, wide_cell_with_padding)
{
	struct grid	*gd = grid_create(10, 1, 0);
	struct grid_cell gc;

	put_wide(gd, 0, 0, "\xe4\xb8\xad");
	put(gd, 2, 0, "x");
	grid_get_cell(gd, 0, 0, &gc);
	CHECK_EQ(gc.data.width, 2);
	grid_get_cell(gd, 1, 0, &gc);
	CHECK(gc.flags & GRID_FLAG_PADDING);
	EXPECT_LINE(gd, 0, "\xe4\xb8\xad" "x");
	grid_destroy(gd);
}

TEST(grid, scroll_history_moves_top_line_and_collects)
{
	struct grid	*gd = grid_create(5, 2, 3);
	char		 buf[8];
	u_int		 i;

	for (i = 0; i < 4; i++) {
		snprintf(buf, sizeof buf, "l%u", i);
		put(gd, 0, gd->hsize, buf);
		grid_scroll_history(gd, 8);
	}
	CHECK_EQ(gd->hsize, 4u);
	CHECK_EQ(gd->hscrolled, 4u);
	EXPECT_LINE(gd, 0, "l0");
	EXPECT_LINE(gd, 3, "l3");

	grid_collect_history(gd, 0);
	CHECK_EQ(gd->hsize, 3u);
	CHECK_EQ(gd->scroll_collected, 1u);
	EXPECT_LINE(gd, 0, "l1");

	grid_clear_history(gd);
	CHECK_EQ(gd->hsize, 0u);
	CHECK_EQ(gd->hscrolled, 0u);
	grid_destroy(gd);
}

TEST(grid, collect_history_is_noop_under_limit)
{
	struct grid	*gd = grid_create(5, 2, 10);

	grid_scroll_history(gd, 8);
	grid_scroll_history(gd, 8);
	grid_collect_history(gd, 0);
	CHECK_EQ(gd->hsize, 2u);
	grid_collect_history(gd, 1);
	CHECK_EQ(gd->hsize, 2u);
	grid_destroy(gd);
}

TEST(grid, clear_and_move_cells)
{
	struct grid	*gd = grid_create(10, 2, 0);

	put(gd, 0, 0, "abcdef");
	grid_clear(gd, 1, 0, 2, 1, 8);
	EXPECT_LINE(gd, 0, "a  def");
	grid_move_cells(gd, 0, 3, 0, 3, 8);	/* dx, px, py, nx */
	EXPECT_LINE(gd, 0, "def");
	put(gd, 0, 1, "0123456789");
	grid_move_cells(gd, 5, 0, 1, 3, 8);
	EXPECT_LINE(gd, 1, "   3401289");
	grid_destroy(gd);
}

TEST(grid, move_lines_and_clear_lines)
{
	struct grid	*gd = grid_create(5, 3, 0);

	put(gd, 0, 0, "one");
	put(gd, 0, 1, "two");
	grid_move_lines(gd, 2, 0, 1, 8);
	EXPECT_LINE(gd, 2, "one");
	EXPECT_LINE(gd, 0, "");
	grid_clear_lines(gd, 1, 2, 8);
	EXPECT_LINE(gd, 1, "");
	EXPECT_LINE(gd, 2, "");
	grid_destroy(gd);
}

TEST(grid, reflow_splits_and_rejoins)
{
	struct grid	*gd = grid_create(10, 3, 100);

	put(gd, 0, 0, "0123456789");
	put(gd, 0, 1, "ab");
	grid_reflow(gd, 5);
	gd->sx = 5;	/* grid_reflow leaves sx to the caller, as screen_resize does */
	CHECK_EQ(gd->hsize + gd->sy, 4u);
	EXPECT_LINE(gd, 0, "01234");
	CHECK(grid_get_line(gd, 0)->flags & GRID_LINE_WRAPPED);
	EXPECT_LINE(gd, 1, "56789");
	EXPECT_LINE(gd, 2, "ab");

	grid_reflow(gd, 10);
	gd->sx = 10;
	CHECK_EQ(gd->hsize, 0u);
	EXPECT_LINE(gd, 0, "0123456789");
	EXPECT_LINE(gd, 1, "ab");
	grid_destroy(gd);
}

TEST(grid, reflow_to_one_column)
{
	struct grid	*gd = grid_create(10, 2, 100);

	put(gd, 0, 0, "abc");
	grid_reflow(gd, 1);
	gd->sx = 1;
	CHECK(gd->hsize + gd->sy >= 3u);
	EXPECT_LINE(gd, 0, "a");
	EXPECT_LINE(gd, 1, "b");
	EXPECT_LINE(gd, 2, "c");
	grid_destroy(gd);
}

TEST(grid, wrap_position_roundtrips)
{
	struct grid	*gd = grid_create(10, 3, 0);
	u_int		 wx, wy, px, py;

	put(gd, 0, 0, "0123456789");
	grid_get_line(gd, 0)->flags |= GRID_LINE_WRAPPED;
	put(gd, 0, 1, "abc");

	grid_wrap_position(gd, 2, 1, &wx, &wy);
	CHECK_EQ(wx, 12u);
	CHECK_EQ(wy, 0u);
	grid_unwrap_position(gd, &px, &py, wx, wy);
	CHECK_EQ(px, 2u);
	CHECK_EQ(py, 1u);

	grid_wrap_position(gd, 3, 0, &wx, &wy);
	CHECK_EQ(wx, 3u);
	CHECK_EQ(wy, 0u);
	grid_destroy(gd);
}

TEST(grid, cells_equal_compares_everything)
{
	struct grid_cell	a, b;

	memcpy(&a, &grid_default_cell, sizeof a);
	memcpy(&b, &grid_default_cell, sizeof b);
	CHECK(grid_cells_equal(&a, &b));
	b.fg = 1;
	CHECK(!grid_cells_equal(&a, &b));
	b.fg = a.fg;
	utf8_set(&b.data, 'z');
	CHECK(!grid_cells_equal(&a, &b));
	utf8_set(&a.data, 'z');
	CHECK(grid_cells_equal(&a, &b));
	b.link = 7;
	CHECK(!grid_cells_equal(&a, &b));
}

TEST(grid, string_cells_flags)
{
	struct grid		*gd = grid_create(10, 1, 0);
	struct grid_cell	 gc, *last = nullptr;
	char			*s;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.attr = GRID_ATTR_BRIGHT;
	grid_set_cells(gd, 0, 0, &gc, "a\\b", 3);
	s = grid_string_cells(gd, 0, 0, 10, nullptr, GRID_STRING_ESCAPE_SEQUENCES, nullptr);
	CHECK_EQ(s, "a\\\\b");
	free(s);
	s = grid_string_cells(gd, 0, 0, 10, &last, GRID_STRING_WITH_SEQUENCES, nullptr);
	CHECK_NONNULL(strstr(s, "\033[1m"));
	CHECK_NONNULL(strstr(s, "a\\b"));
	free(s);
	grid_destroy(gd);
}

TEST(grid, view_indexes_below_history)
{
	struct grid	*gd = grid_create(5, 3, 10);
	struct grid_cell gc;
	char		*s;

	grid_view_set_cells(gd, 0, 0, &grid_default_cell, "abc", 3);
	grid_scroll_history(gd, 8);
	CHECK_EQ(gd->hsize, 1u);
	EXPECT_LINE(gd, 0, "abc");
	grid_view_get_cell(gd, 0, 0, &gc);
	CHECK(grid_cells_equal(&gc, &grid_default_cell));
	grid_view_set_cells(gd, 0, 0, &grid_default_cell, "xyz", 3);
	s = grid_view_string_cells(gd, 0, 0, 5);
	CHECK(strncmp(s, "xyz", 3) == 0);
	free(s);
	EXPECT_VIEW(gd, 0, "xyz");
	EXPECT_LINE(gd, 1, "xyz");
	grid_destroy(gd);
}

TEST(grid, view_insert_and_delete_cells)
{
	struct grid	*gd = grid_create(6, 1, 0);

	grid_view_set_cells(gd, 0, 0, &grid_default_cell, "abc", 3);
	grid_view_delete_cells(gd, 1, 0, 1, 8);
	EXPECT_VIEW(gd, 0, "ac");
	grid_view_insert_cells(gd, 1, 0, 2, 8);
	EXPECT_VIEW(gd, 0, "a  c");
	grid_destroy(gd);
}

TEST(grid, view_insert_and_delete_lines)
{
	struct grid	*gd = grid_create(5, 3, 0);

	grid_view_set_cells(gd, 0, 0, &grid_default_cell, "one", 3);
	grid_view_set_cells(gd, 0, 1, &grid_default_cell, "two", 3);
	grid_view_insert_lines(gd, 0, 1, 8);
	EXPECT_VIEW(gd, 0, "");
	EXPECT_VIEW(gd, 1, "one");
	grid_view_delete_lines(gd, 0, 2, 8);
	EXPECT_VIEW(gd, 0, "two");
	grid_destroy(gd);
}

TEST(grid, view_scroll_region_up_into_history)
{
	struct grid	*gd = grid_create(5, 3, 10);

	grid_view_set_cells(gd, 0, 0, &grid_default_cell, "top", 3);
	grid_view_set_cells(gd, 0, 1, &grid_default_cell, "mid", 3);
	grid_view_scroll_region_up(gd, 0, 2, 8);
	CHECK_EQ(gd->hsize, 1u);
	EXPECT_LINE(gd, 0, "top");
	EXPECT_VIEW(gd, 0, "mid");
	grid_destroy(gd);
}

TEST(grid, view_scroll_region_down_keeps_history)
{
	struct grid	*gd = grid_create(5, 3, 10);

	grid_view_set_cells(gd, 0, 0, &grid_default_cell, "top", 3);
	grid_view_scroll_region_down(gd, 0, 2, 8);
	CHECK_EQ(gd->hsize, 0u);
	EXPECT_VIEW(gd, 1, "top");
	EXPECT_VIEW(gd, 0, "");
	grid_destroy(gd);
}

TEST(grid, reader_word_motion)
{
	struct grid		*gd = grid_create(20, 2, 0);
	struct grid_reader	 gr;
	u_int			 x, y;

	put(gd, 0, 0, "foo bar-baz  qux");
	grid_reader_start(&gr, gd, 0, 0);
	CHECK_EQ(grid_reader_line_length(&gr), 16u);

	/* Like vi's w: a run of non-blank separators is a word of its own. */
	grid_reader_cursor_next_word(&gr, " -");
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 4u);
	grid_reader_cursor_next_word(&gr, " -");
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 7u);
	grid_reader_cursor_next_word(&gr, " -");
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 8u);
	grid_reader_cursor_next_word(&gr, " -");
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 13u);
	CHECK_EQ(y, 0u);

	/* already=0 goes to the start of this word; already=1 to the previous one. */
	grid_reader_cursor_previous_word(&gr, " -", 0, 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 13u);
	grid_reader_cursor_previous_word(&gr, " -", 1, 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 8u);
	grid_reader_start(&gr, gd, 10, 0);
	grid_reader_cursor_previous_word(&gr, " -", 0, 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 8u);

	grid_reader_start(&gr, gd, 0, 0);
	/* Lands just past the word; copy mode steps back one for vi's e. */
	grid_reader_cursor_next_word_end(&gr, " -");
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 3u);

	grid_reader_start(&gr, gd, 3, 0);
	CHECK_EQ(grid_reader_in_set(&gr, " "), 1);
	CHECK_EQ(grid_reader_in_set(&gr, "-"), 0);
	grid_destroy(gd);
}

TEST(grid, reader_line_motion_and_jump)
{
	struct grid		*gd = grid_create(20, 2, 0);
	struct grid_reader	 gr;
	struct utf8_data	 ud;
	u_int			 x, y;

	put(gd, 0, 0, "   indented text");
	grid_reader_start(&gr, gd, 10, 0);
	grid_reader_cursor_back_to_indentation(&gr);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 3u);

	grid_reader_cursor_start_of_line(&gr, 0);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 0u);
	grid_reader_cursor_end_of_line(&gr, 0, 0);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 16u);

	grid_reader_start(&gr, gd, 0, 0);
	utf8_set(&ud, 't');
	CHECK_EQ(grid_reader_cursor_jump(&gr, &ud), 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 8u);
	utf8_set(&ud, 'z');
	CHECK_EQ(grid_reader_cursor_jump(&gr, &ud), 0);

	grid_reader_cursor_right(&gr, 1, 0, 1);
	grid_reader_cursor_left(&gr, 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 8u);
	grid_reader_cursor_down(&gr);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(y, 1u);
	grid_reader_cursor_up(&gr);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(y, 0u);
	grid_destroy(gd);
}
