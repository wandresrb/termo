#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

/* A window with n tiled panes split left-to-right, no ptys, no server. */
static struct window *
make_window(u_int sx, u_int sy, u_int n)
{
	struct window		*w = window_create(sx, sy, 0, 0);
	struct window_pane	*wp, *prev;
	struct layout_cell	*lc;
	u_int			 i;

	window_add_ref(w, __func__);
	prev = window_add_pane(w, nullptr, 0, 0);
	window_set_active_pane(w, prev, 0);
	layout_init(w, prev);
	for (i = 1; i < n; i++) {
		lc = layout_split_pane(prev, LAYOUT_LEFTRIGHT, -1, 0);
		if (lc == nullptr)
			break;
		wp = window_add_pane(w, prev, 0, 0);
		layout_assign_pane(lc, wp, 0);
		prev = wp;
	}
	return (w);
}

static void
drop_window(struct window *w)
{
	window_remove_ref(w, __func__);
}

TEST(layout, init_makes_one_full_size_leaf)
{
	struct window		*w = make_window(80, 24, 1);
	struct layout_cell	*lc = w->layout_root;

	REQUIRE_NONNULL(lc);
	CHECK_EQ(lc->type, LAYOUT_WINDOWPANE);
	CHECK_EQ(lc->g.sx, 80u);
	CHECK_EQ(lc->g.sy, 24u);
	CHECK_EQ(layout_count_cells(lc), 1u);
	CHECK(lc->wp == TAILQ_FIRST(&w->panes));
	drop_window(w);
}

TEST(layout, split_halves_and_leaves_a_border)
{
	struct window		*w = make_window(80, 24, 2);
	struct layout_cell	*root = w->layout_root, *a, *b;

	REQUIRE_EQ(root->type, LAYOUT_LEFTRIGHT);
	CHECK_EQ(layout_count_cells(root), 2u);
	a = TAILQ_FIRST(&root->cells);
	b = TAILQ_NEXT(a, entry);
	REQUIRE_NONNULL(b);
	CHECK_EQ(a->g.xoff, 0u);
	CHECK_EQ(a->g.sx + 1 + b->g.sx, 80u);
	CHECK_EQ(b->g.xoff, a->g.sx + 1);
	CHECK_EQ(a->g.sy, 24u);
	CHECK_EQ(b->g.sy, 24u);
	drop_window(w);
}

TEST(layout, dump_has_checksum_and_parses_back)
{
	struct window	*w = make_window(80, 24, 2);
	char		*dump, *again, *cause = nullptr;

	dump = layout_dump(w, w->layout_root);
	REQUIRE_NONNULL(dump);
	CHECK_EQ(strlen(dump) > 5 && dump[4] == ',', 1);
	CHECK_NONNULL(strstr(dump, "80x24,0,0{"));

	CHECK_EQ(layout_parse(w, dump, &cause), 0);
	CHECK_NULL(cause);
	again = layout_dump(w, w->layout_root);
	CHECK_EQ(again, dump);
	free(again);
	free(dump);
	drop_window(w);
}

TEST(layout, parse_rejects_bad_checksum_and_syntax)
{
	struct window	*w = make_window(80, 24, 2);
	char		*dump, *cause = nullptr;

	dump = layout_dump(w, w->layout_root);
	REQUIRE_NONNULL(dump);
	dump[0] = dump[0] == 'f' ? 'e' : 'f';
	CHECK_EQ(layout_parse(w, dump, &cause), -1);
	CHECK_EQ(cause, "invalid layout");
	free(cause);
	cause = nullptr;
	free(dump);

	CHECK_EQ(layout_parse(w, "garbage", &cause), -1);
	CHECK_EQ(cause, "invalid layout");
	free(cause);
	cause = nullptr;
	CHECK_EQ(layout_parse(w, "", &cause), -1);
	free(cause);
	drop_window(w);
}

TEST(layout, parse_needs_enough_cells_for_the_panes)
{
	struct window	*one = make_window(80, 24, 1), *two = make_window(80, 24, 2);
	char		*dump, *cause = nullptr;

	dump = layout_dump(one, one->layout_root);
	REQUIRE_NONNULL(dump);
	CHECK_EQ(layout_parse(two, dump, &cause), -1);
	REQUIRE_NONNULL(cause);
	CHECK_NONNULL(strstr(cause, "have 2 panes but need 1"));
	free(cause);
	cause = nullptr;

	/* More cells than panes is fine: the extra cells are dropped. */
	free(dump);
	dump = layout_dump(two, two->layout_root);
	CHECK_EQ(layout_parse(one, dump, &cause), 0);
	CHECK_EQ(layout_count_cells(one->layout_root), 1u);
	free(dump);
	drop_window(one);
	drop_window(two);
}

/* The layout takes the dumped geometry; the window itself is not resized. */
TEST(layout, parse_applies_the_dumped_geometry)
{
	struct window	*small = make_window(40, 10, 1), *w = make_window(80, 24, 1);
	char		*dump, *cause = nullptr;

	dump = layout_dump(small, small->layout_root);
	REQUIRE_NONNULL(dump);
	CHECK_NONNULL(strstr(dump, ",40x10,0,0"));
	CHECK_EQ(layout_parse(w, dump, &cause), 0);
	CHECK_NULL(cause);
	CHECK_EQ(w->layout_root->g.sx, 40u);
	CHECK_EQ(w->layout_root->g.sy, 10u);
	CHECK_EQ(w->sx, 80u);
	free(dump);
	drop_window(small);
	drop_window(w);
}

TEST(layout, presets_by_name)
{
	struct window	*w = make_window(80, 24, 3);
	char		*dump;

	CHECK_EQ(layout_set_lookup("even-horizontal"), 0);
	CHECK_EQ(layout_set_lookup("even-vertical"), 1);
	CHECK_EQ(layout_set_lookup("main-vertical"), 4);
	CHECK_EQ(layout_set_lookup("tiled"), 6);
	CHECK_EQ(layout_set_lookup("nope"), -1);

	CHECK_EQ(layout_set_select(w, 1), 1u);
	CHECK_EQ(w->layout_root->type, LAYOUT_TOPBOTTOM);
	dump = layout_dump(w, w->layout_root);
	CHECK_NONNULL(strchr(dump, '['));
	free(dump);

	CHECK_EQ(layout_set_select(w, 0), 0u);
	CHECK_EQ(w->layout_root->type, LAYOUT_LEFTRIGHT);
	CHECK_EQ(layout_count_cells(w->layout_root), 3u);

	CHECK_EQ(layout_set_next(w), 1u);
	CHECK_EQ(layout_set_previous(w), 0u);
	drop_window(w);
}

TEST(layout, resize_keeps_every_pane_at_least_minimum)
{
	struct window		*w = make_window(80, 24, 3);
	struct layout_cell	*lc;
	u_int			 total = 0;

	layout_resize(w, 20, 5);
	CHECK_EQ(w->layout_root->g.sx, 20u);
	TAILQ_FOREACH(lc, &w->layout_root->cells, entry) {
		CHECK(lc->g.sx >= PANE_MINIMUM);
		CHECK_EQ(lc->g.sy, 5u);
		total += lc->g.sx;
	}
	CHECK_EQ(total + 2, 20u);
	drop_window(w);
}

TEST(layout, split_refuses_when_no_space)
{
	struct window		*w = make_window(3, 5, 1);
	struct window_pane	*wp = TAILQ_FIRST(&w->panes);

	CHECK_NONNULL(layout_split_pane(wp, LAYOUT_LEFTRIGHT, -1, 0));
	drop_window(w);

	w = make_window(1, 5, 1);
	wp = TAILQ_FIRST(&w->panes);
	CHECK_NULL(layout_split_pane(wp, LAYOUT_LEFTRIGHT, -1, 0));
	drop_window(w);
}

TEST(layout, close_pane_returns_space_to_sibling)
{
	struct window		*w = make_window(80, 24, 2);
	struct window_pane	*second = TAILQ_LAST(&w->panes, window_panes);

	layout_close_pane(second);
	CHECK_EQ(w->layout_root->type, LAYOUT_WINDOWPANE);
	CHECK_EQ(w->layout_root->g.sx, 80u);
	CHECK_EQ(layout_count_cells(w->layout_root), 1u);
	drop_window(w);
}

/* Destroying a zoomed window fires pane-resized while the refcount is 0. */
TEST(layout, destroying_a_zoomed_window_does_not_reenter_destroy)
{
	struct window	*w = make_window(80, 24, 2);

	CHECK_EQ(window_zoom(TAILQ_FIRST(&w->panes)), 0);
	CHECK(w->flags & WINDOW_ZOOMED);
	drop_window(w);
	CHECK(1);
}
