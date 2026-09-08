#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"
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

	termo_test_wide(&gc, s);
	grid_set_cell(gd, x, y, &gc);
	grid_set_padding(gd, x + 1, y, 0);
}

static void
put_rows(struct grid *gd, const char *rows)
{
	char	s[2] = { '\0', '\0' };
	u_int	y;

	for (y = 0; rows[y] != '\0'; y++) {
		s[0] = rows[y];
		put(gd, 0, gd->hsize + y, s);
	}
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

TEST(grid, view_insert_delete_lines_region_stop_at_rlower)
{
	struct grid	*gd = grid_create(5, 4, 0);

	put_rows(gd, "abcd");
	/* ny < ny2 underflows ny - ny2; the trailing grid_clear is a no-op. */
	grid_view_insert_lines_region(gd, 2, 0, 1, 8);
	EXPECT_VIEW(gd, 0, "");
	EXPECT_VIEW(gd, 1, "a");
	EXPECT_VIEW(gd, 2, "b");
	EXPECT_VIEW(gd, 3, "d");
	grid_view_delete_lines_region(gd, 2, 0, 1, 8);
	EXPECT_VIEW(gd, 0, "a");
	EXPECT_VIEW(gd, 1, "b");
	EXPECT_VIEW(gd, 2, "");
	EXPECT_VIEW(gd, 3, "d");
	grid_destroy(gd);

	gd = grid_create(5, 4, 0);
	put_rows(gd, "abcd");
	grid_view_insert_lines_region(gd, 3, 1, 2, 8);
	EXPECT_VIEW(gd, 0, "a");
	EXPECT_VIEW(gd, 1, "");
	EXPECT_VIEW(gd, 2, "");
	EXPECT_VIEW(gd, 3, "b");
	grid_destroy(gd);
}

TEST(grid, view_scroll_region_up_partial_region_feeds_history)
{
	struct grid	*gd = grid_create(5, 4, 10);

	put_rows(gd, "abcd");
	grid_view_scroll_region_up(gd, 1, 2, 8);
	CHECK_EQ(gd->hsize, 1u);
	CHECK_EQ(gd->hscrolled, 1u);
	EXPECT_LINE(gd, 0, "b");
	EXPECT_VIEW(gd, 0, "a");
	EXPECT_VIEW(gd, 1, "c");
	EXPECT_VIEW(gd, 2, "");
	EXPECT_VIEW(gd, 3, "d");
	grid_destroy(gd);

	gd = grid_create(5, 4, 0);
	put_rows(gd, "abcd");
	grid_view_scroll_region_up(gd, 1, 2, 8);
	CHECK_EQ(gd->hsize, 0u);
	EXPECT_VIEW(gd, 0, "a");
	EXPECT_VIEW(gd, 1, "c");
	EXPECT_VIEW(gd, 2, "");
	EXPECT_VIEW(gd, 3, "d");
	grid_destroy(gd);
}

TEST(grid, remove_history_drops_bottom_lines_and_shifts_view)
{
	struct grid	*gd = grid_create(5, 2, 10);

	put(gd, 0, 0, "h0");
	grid_scroll_history(gd, 8);
	put(gd, 0, 1, "h1");
	grid_scroll_history(gd, 8);
	put(gd, 0, 2, "v0");
	put(gd, 0, 3, "v1");
	CHECK_EQ(gd->hsize, 2u);

	grid_remove_history(gd, 1);
	CHECK_EQ(gd->hsize, 1u);
	EXPECT_LINE(gd, 0, "h0");
	EXPECT_VIEW(gd, 0, "h1");
	EXPECT_VIEW(gd, 1, "v0");

	grid_remove_history(gd, 5);
	CHECK_EQ(gd->hsize, 1u);
	EXPECT_VIEW(gd, 1, "v0");
	grid_destroy(gd);
}

TEST(grid, collect_history_trims_ten_percent_then_all_over_limit)
{
	struct grid	*gd = grid_create(5, 2, 20);
	char		 buf[8];
	u_int		 i;

	for (i = 0; i < 25; i++) {
		snprintf(buf, sizeof buf, "l%u", i);
		put(gd, 0, gd->hsize, buf);
		grid_scroll_history(gd, 8);
	}
	CHECK_EQ(gd->hsize, 25u);

	grid_collect_history(gd, 0);
	CHECK_EQ(gd->hsize, 23u);
	CHECK_EQ(gd->scroll_collected, 2u);
	EXPECT_LINE(gd, 0, "l2");

	grid_collect_history(gd, 1);
	CHECK_EQ(gd->hsize, 20u);
	CHECK_EQ(gd->scroll_collected, 5u);
	CHECK_EQ(gd->hscrolled, 20u);
	EXPECT_LINE(gd, 0, "l5");
	EXPECT_LINE(gd, 19, "l24");
	grid_destroy(gd);
}

TEST(grid, bounds_out_of_range_is_ignored_or_default)
{
	struct grid	*gd = grid_create(5, 3, 0);
	char		*s;

	put(gd, 0, 0, "abc");
	CHECK_NULL(grid_peek_line(gd, gd->hsize + gd->sy));
	s = grid_string_cells(gd, 0, 99, 5, nullptr, 0, nullptr);
	CHECK_EQ(s, "");
	free(s);

	grid_move_lines(gd, 0, 5, 1, 8);
	grid_move_lines(gd, 5, 0, 1, 8);
	grid_clear_lines(gd, 7, 1, 8);
	grid_clear(gd, 0, 9, 2, 1, 8);
	grid_clear(gd, 0, 9, 5, 1, 8);
	grid_set_cells(gd, 0, 9, &grid_default_cell, "zz", 2);
	EXPECT_LINE(gd, 0, "abc");
	CHECK_EQ(gd->hsize, 0u);
	grid_destroy(gd);
}

TEST(grid, string_cells_with_sequences_exact_sgr_output)
{
	struct grid		*gd = grid_create(10, 1, 0);
	struct grid_cell	 gc, *last = nullptr;
	char			*s;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.attr = GRID_ATTR_BRIGHT;
	gc.fg = 1;
	grid_set_cells(gd, 0, 0, &gc, "A", 1);
	put(gd, 1, 0, "B");
	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.bg = 200|COLOUR_FLAG_256;
	grid_set_cells(gd, 2, 0, &gc, "C", 1);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.fg = 0x0a141e|COLOUR_FLAG_RGB;
	gc.attr = GRID_ATTR_UNDERSCORE_3;
	grid_set_cells(gd, 3, 0, &gc, "D", 1);

	s = grid_string_cells(gd, 0, 0, 10, &last, GRID_STRING_WITH_SEQUENCES,
	    nullptr);
	CHECK_EQ(s, "\033[1m\033[31mA\033[0mB\033[48;5;200mC"
	    "\033[4:3m\033[38;2;10;20;30m\033[49mD");
	free(s);

	last = nullptr;
	s = grid_string_cells(gd, 0, 0, 10, &last,
	    GRID_STRING_WITH_SEQUENCES|GRID_STRING_ESCAPE_SEQUENCES, nullptr);
	CHECK_EQ(s, "\\033[1m\\033[31mA\\033[0mB\\033[48;5;200mC"
	    "\\033[4:3m\\033[38;2;10;20;30m\\033[49mD");
	free(s);
	grid_destroy(gd);
}

TEST(grid, string_cells_emits_hyperlink_open_and_close)
{
	struct screen		 s;
	struct grid_cell	 gc, *last = nullptr;
	u_int			 l1, l2;
	char			*out;

	screen_init(&s, 10, 1, 0);
	screen_reset_hyperlinks(&s);
	l1 = hyperlinks_put(s.hyperlinks, "http://x.test", nullptr);
	l2 = hyperlinks_put(s.hyperlinks, "http://y.test", "myid");
	CHECK(l1 != 0);
	CHECK(l2 != 0 && l2 != l1);

	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.link = l1;
	grid_set_cells(s.grid, 0, 0, &gc, "ab", 2);
	put(s.grid, 2, 0, "c");
	gc.link = l2;
	grid_set_cells(s.grid, 3, 0, &gc, "d", 1);
	CHECK(grid_get_line(s.grid, 0)->flags & GRID_LINE_HYPERLINK);

	out = grid_string_cells(s.grid, 0, 0, 10, &last,
	    GRID_STRING_WITH_SEQUENCES, &s);
	/* The final close is appended to the last cell's code, repeating it. */
	CHECK_EQ(out, "\033]8;;http://x.test\033\\ab\033]8;;\033\\c"
	    "\033]8;id=myid;http://y.test\033\\d"
	    "\033]8;id=myid;http://y.test\033\\\033]8;;\033\\");
	free(out);
	screen_free(&s);
}

TEST(grid, line_limit_and_in_set_see_through_padding_and_tabs)
{
	struct grid		*gd = grid_create(10, 1, 0);
	struct grid_cell	 gc;
	u_int			 x;

	put_wide(gd, 3, 0, "\xe4\xb8\xad");
	CHECK_EQ(grid_line_length(gd, 0), 5u);
	CHECK_EQ(grid_line_limit(gd, 0), 3u);
	grid_destroy(gd);

	gd = grid_create(10, 1, 0);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	grid_set_tab(&gc, 4);
	grid_view_set_cell(gd, 0, 0, &gc);
	for (x = 1; x < 4; x++)
		grid_view_set_padding(gd, x, 0, 8);

	grid_get_cell(gd, 0, 0, &gc);
	CHECK(gc.flags & GRID_FLAG_TAB);
	CHECK_EQ(gc.data.width, 4);
	EXPECT_LINE(gd, 0, "\t");
	CHECK_EQ(grid_in_set(gd, 0, 0, "\t"), 4);
	CHECK_EQ(grid_in_set(gd, 2, 0, "\t"), 2);
	CHECK_EQ(grid_in_set(gd, 2, 0, " "), 0);
	CHECK_EQ(grid_in_set(gd, 5, 0, " "), 1);
	grid_destroy(gd);
}

TEST(grid, reader_cursor_jump_and_jump_back_cross_wrapped_lines)
{
	struct grid		*gd = grid_create(10, 3, 0);
	struct grid_reader	 gr;
	struct utf8_data	 ud;
	u_int			 x, y;

	put(gd, 0, 0, "abcXdefghi");
	grid_get_line(gd, 0)->flags |= GRID_LINE_WRAPPED;
	put(gd, 0, 1, "jklmXno");
	put(gd, 0, 2, "zzz");
	utf8_set(&ud, 'X');

	grid_reader_start(&gr, gd, 3, 1);
	CHECK_EQ(grid_reader_cursor_jump_back(&gr, &ud), 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 3u);
	CHECK_EQ(y, 0u);

	grid_reader_start(&gr, gd, 2, 0);
	CHECK_EQ(grid_reader_cursor_jump_back(&gr, &ud), 0);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 2u);
	CHECK_EQ(y, 0u);

	grid_reader_start(&gr, gd, 5, 0);
	CHECK_EQ(grid_reader_cursor_jump(&gr, &ud), 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 4u);
	CHECK_EQ(y, 1u);

	grid_get_line(gd, 0)->flags &= ~GRID_LINE_WRAPPED;
	grid_reader_start(&gr, gd, 3, 1);
	CHECK_EQ(grid_reader_cursor_jump_back(&gr, &ud), 0);
	grid_reader_start(&gr, gd, 5, 0);
	CHECK_EQ(grid_reader_cursor_jump(&gr, &ud), 0);
	grid_destroy(gd);
}

TEST(grid, reader_edge_motion_wraps_and_clamps)
{
	struct grid		*gd = grid_create(6, 3, 0);
	struct grid_reader	 gr;
	u_int			 x, y;

	put(gd, 0, 0, "abcdef");
	grid_get_line(gd, 0)->flags |= GRID_LINE_WRAPPED;
	put(gd, 0, 1, "gh");

	grid_reader_start(&gr, gd, 5, 0);
	grid_reader_cursor_right(&gr, 0, 0, 0);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 5u);
	CHECK_EQ(y, 0u);
	grid_reader_cursor_right(&gr, 1, 0, 0);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 0u);
	CHECK_EQ(y, 1u);

	grid_reader_cursor_left(&gr, 0);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 6u);
	CHECK_EQ(y, 0u);

	grid_reader_start(&gr, gd, 1, 1);
	grid_reader_cursor_start_of_line(&gr, 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 0u);
	CHECK_EQ(y, 0u);

	grid_reader_cursor_end_of_line(&gr, 1, 0);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 2u);
	CHECK_EQ(y, 1u);

	grid_reader_cursor_end_of_line(&gr, 0, 1);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(x, 6u);
	CHECK_EQ(y, 1u);

	grid_reader_start(&gr, gd, 0, 2);
	grid_reader_cursor_down(&gr);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(y, 2u);

	grid_reader_start(&gr, gd, 0, 0);
	grid_reader_cursor_up(&gr);
	grid_reader_get_cursor(&gr, &x, &y);
	CHECK_EQ(y, 0u);
	grid_destroy(gd);
}

TEST(grid, duplicate_lines_and_compare)
{
	struct grid		*a = grid_create(5, 2, 0);
	struct grid		*b = grid_create(5, 2, 0);
	struct grid		*c = grid_create(5, 1, 0);
	struct grid_cell	 gc, x, y;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.attr = GRID_ATTR_BRIGHT;
	grid_set_cells(a, 0, 0, &gc, "ab", 2);
	put_wide(a, 0, 1, "\xe4\xb8\xad");

	grid_duplicate_lines(b, 0, a, 0, 2);
	CHECK_EQ(grid_compare(a, b), 0);
	EXPECT_LINE(b, 0, "ab");
	EXPECT_LINE(b, 1, "\xe4\xb8\xad");
	grid_get_cell(b, 0, 0, &gc);
	CHECK_EQ(gc.attr, GRID_ATTR_BRIGHT);
	grid_get_cell(b, 1, 1, &gc);
	CHECK(gc.flags & GRID_FLAG_PADDING);

	put(b, 0, 0, "zz");
	CHECK_EQ(grid_compare(a, b), 1);

	grid_duplicate_lines(c, 0, a, 0, 2);
	EXPECT_LINE(c, 0, "ab");
	CHECK_EQ(grid_compare(a, c), 1);

	memcpy(&x, &grid_default_cell, sizeof x);
	memcpy(&y, &grid_default_cell, sizeof y);
	utf8_set(&y.data, 'q');
	CHECK_EQ(grid_cells_look_equal(&x, &y), 1);
	y.flags |= GRID_FLAG_CLEARED;
	CHECK_EQ(grid_cells_look_equal(&x, &y), 1);
	y.link = 3;
	CHECK_EQ(grid_cells_look_equal(&x, &y), 0);

	grid_destroy(a);
	grid_destroy(b);
	grid_destroy(c);
}

TEST(grid, flag_strings_name_bits)
{
	CHECK_EQ(grid_line_flags_string(0), "NONE");
	CHECK_EQ(grid_cell_flags_string(0), "NONE");
	CHECK_EQ(grid_cell_attr_string(0), "NONE");
	CHECK_EQ(grid_line_flags_string(GRID_LINE_WRAPPED|GRID_LINE_EXTENDED),
	    "WRAPPED,EXTENDED");
	CHECK_EQ(grid_cell_flags_string(GRID_FLAG_PADDING|GRID_FLAG_TAB),
	    "PADDING,TAB");
	CHECK_EQ(grid_cell_attr_string(GRID_ATTR_BRIGHT|GRID_ATTR_UNDERSCORE_3|
	    GRID_ATTR_OVERLINE), "BRIGHT,UNDERSCORE_3,OVERLINE");
}

TEST(grid, view_clear_history_scrolls_only_used_lines)
{
	struct grid	*gd = grid_create(5, 4, 10);
	u_int		 y;

	put_rows(gd, "ab");
	grid_view_clear_history(gd, 8);
	CHECK_EQ(gd->hsize, 2u);
	CHECK_EQ(gd->hscrolled, 0u);
	EXPECT_LINE(gd, 0, "a");
	EXPECT_LINE(gd, 1, "b");
	for (y = 0; y < gd->sy; y++)
		EXPECT_VIEW(gd, y, "");
	grid_destroy(gd);

	gd = grid_create(5, 4, 10);
	grid_scroll_history(gd, 8);
	grid_view_clear_history(gd, 8);
	CHECK_EQ(gd->hsize, 1u);
	grid_destroy(gd);
}

TEST(grid, empty_line_and_clear_with_background_colour)
{
	struct grid		*gd = grid_create(5, 2, 0);
	struct grid_cell	 gc;

	grid_empty_line(gd, 0, 4);
	CHECK_EQ(grid_get_line(gd, 0)->cellsize, gd->sx);
	CHECK_EQ(grid_get_line(gd, 0)->cellused, 0u);
	grid_get_cell(gd, 0, 0, &gc);
	CHECK_EQ(gc.bg, 4);
	CHECK(gc.flags & GRID_FLAG_CLEARED);
	grid_get_cell(gd, 4, 0, &gc);
	CHECK_EQ(gc.bg, 4);

	grid_free_lines(gd, 0, 1);
	grid_empty_line(gd, 0, 8);
	CHECK_EQ(grid_get_line(gd, 0)->cellsize, 0u);

	grid_view_clear(gd, 1, 1, 2, 1, 0x112233|COLOUR_FLAG_RGB);
	CHECK(grid_get_line(gd, 1)->flags & GRID_LINE_EXTENDED);
	grid_get_cell(gd, 1, 1, &gc);
	CHECK_EQ(gc.bg, 0x112233|COLOUR_FLAG_RGB);
	grid_get_cell(gd, 2, 1, &gc);
	CHECK_EQ(gc.bg, 0x112233|COLOUR_FLAG_RGB);
	grid_get_cell(gd, 3, 1, &gc);
	CHECK_EQ(gc.bg, 8);
	grid_destroy(gd);
}
