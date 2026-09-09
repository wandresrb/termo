#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"
#include "test.h"

#define make_window(sx, sy, n)	termo_test_window(sx, sy, n)
#define drop_window(w)		termo_test_window_free(w)

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

struct teardown_resize {
	struct window	*w;
	u_int		 fired;
};

static void
teardown_resize_cb([[maybe_unused]] const char *name, struct event_payload *ep,
    void *data)
{
	struct teardown_resize	*tr = data;
	struct window		*w = event_payload_get_window(ep, "window");

	tr->fired++;
	CHECK(w == tr->w);
	CHECK(w->flags & WINDOW_DESTROYING);
	CHECK(!(w->flags & WINDOW_ZOOMED));
	CHECK_NULL(w->saved_layout_root);
	CHECK_EQ(w->references, 1);
}

/*
 * Destroying a zoomed window unzooms it, which fires pane-resized with the
 * payload holding the only reference; dropping it must not destroy again.
 */
TEST(layout, destroying_a_zoomed_window_does_not_reenter_destroy)
{
	struct window		*w = make_window(80, 24, 2);
	struct events_sink	*es;
	struct teardown_resize	 tr = { w, 0 };
	u_int			 id = w->id;

	CHECK_EQ(window_zoom(TAILQ_FIRST(&w->panes)), 0);
	CHECK(w->flags & WINDOW_ZOOMED);
	CHECK_NONNULL(w->saved_layout_root);
	CHECK_EQ(w->references, 1);

	es = events_add_sink("pane-resized", teardown_resize_cb, &tr);
	drop_window(w);
	events_remove_sink(es);

	CHECK_EQ(tr.fired, 1u);
	CHECK_NULL(window_find_by_id(id));
}

TEST(layout, split_check_space_and_split_sizes_honour_minimum)
{
	struct window		*w = make_window(80, 24, 1);
	struct window_pane	*wp = TAILQ_FIRST(&w->panes);
	struct layout_cell	*lc = w->layout_root;
	u_int			 s1, s2, saved;

	layout_split_sizes(lc, -1, 0, LAYOUT_LEFTRIGHT, &s1, &s2, &saved);
	CHECK_EQ(s1, 40u);
	CHECK_EQ(s2, 39u);
	CHECK_EQ(saved, 80u);
	layout_split_sizes(lc, 10, 0, LAYOUT_LEFTRIGHT, &s1, &s2, &saved);
	CHECK_EQ(s1, 69u);
	CHECK_EQ(s2, 10u);
	layout_split_sizes(lc, 10, 1, LAYOUT_LEFTRIGHT, &s1, &s2, &saved);
	CHECK_EQ(s1, 10u);
	CHECK_EQ(s2, 69u);
	layout_split_sizes(lc, 0, 0, LAYOUT_LEFTRIGHT, &s1, &s2, &saved);
	CHECK_EQ(s1, 78u);
	CHECK_EQ(s2, 1u);
	layout_split_sizes(lc, 200, 0, LAYOUT_LEFTRIGHT, &s1, &s2, &saved);
	CHECK_EQ(s1, 1u);
	CHECK_EQ(s2, 78u);
	CHECK_EQ(saved, 80u);

	layout_set_size(lc, 3, 5, 0, 0);
	CHECK_EQ(layout_split_check_space(wp, lc, LAYOUT_LEFTRIGHT), 1);
	layout_set_size(lc, 2, 5, 0, 0);
	CHECK_EQ(layout_split_check_space(wp, lc, LAYOUT_LEFTRIGHT), 0);
	layout_set_size(lc, 5, 3, 0, 0);
	CHECK_EQ(layout_split_check_space(wp, lc, LAYOUT_TOPBOTTOM), 1);
	layout_set_size(lc, 5, 2, 0, 0);
	CHECK_EQ(layout_split_check_space(wp, lc, LAYOUT_TOPBOTTOM), 0);

	options_set_number(global_w_options, "pane-border-status",
	    PANE_STATUS_TOP);
	layout_set_size(lc, 5, 3, 0, 0);
	CHECK_EQ(layout_split_check_space(wp, lc, LAYOUT_TOPBOTTOM), 0);
	layout_set_size(lc, 5, 4, 0, 0);
	CHECK_EQ(layout_split_check_space(wp, lc, LAYOUT_TOPBOTTOM), 1);
	drop_window(w);
}

TEST(layout, resize_pane_grows_shrinks_and_clamps)
{
	struct window		*w = make_window(80, 24, 2);
	struct window_pane	*a = TAILQ_FIRST(&w->panes);
	struct window_pane	*b = TAILQ_NEXT(a, entry);
	struct layout_cell	*la = a->layout_cell, *lb = b->layout_cell;

	layout_resize_pane(a, LAYOUT_LEFTRIGHT, 10, 0);
	CHECK_EQ(la->g.sx, 50u);
	CHECK_EQ(lb->g.sx, 29u);
	CHECK_EQ(lb->g.xoff, 51);
	CHECK_EQ(b->xoff, 51);

	layout_resize_pane(a, LAYOUT_LEFTRIGHT, -5, 0);
	CHECK_EQ(la->g.sx, 45u);
	CHECK_EQ(lb->g.sx, 34u);

	layout_resize_pane(a, LAYOUT_LEFTRIGHT, 1000, 0);
	CHECK_EQ(la->g.sx, 78u);
	CHECK_EQ(lb->g.sx, 1u);

	layout_resize_pane(a, LAYOUT_LEFTRIGHT, -1000, 0);
	CHECK_EQ(la->g.sx, 1u);
	CHECK_EQ(lb->g.sx, 78u);

	layout_resize_pane(a, LAYOUT_TOPBOTTOM, 5, 0);
	CHECK_EQ(la->g.sx, 1u);
	CHECK_EQ(la->g.sy, 24u);
	CHECK_EQ(lb->g.sx, 78u);

	layout_resize_pane(b, LAYOUT_LEFTRIGHT, 10, 0);
	CHECK_EQ(la->g.sx, 11u);
	CHECK_EQ(lb->g.sx, 68u);

	layout_resize_pane_to(a, LAYOUT_LEFTRIGHT, 20);
	CHECK_EQ(la->g.sx, 20u);
	CHECK_EQ(lb->g.sx, 59u);
	CHECK_EQ(a->sx, 20u);
	CHECK_EQ(b->sx, 59u);

	layout_resize_pane_to(b, LAYOUT_LEFTRIGHT, 20);
	CHECK_EQ(la->g.sx, 59u);
	CHECK_EQ(lb->g.sx, 20u);
	CHECK_EQ(b->xoff, 60);
	drop_window(w);
}

static void
check_columns(struct window *w, const u_int *sx)
{
	struct window_pane	*wp;
	int			 xoff = 0;
	u_int			 i = 0;

	TAILQ_FOREACH(wp, &w->panes, entry) {
		CHECK_EQ(wp->layout_cell->g.sx, sx[i]);
		CHECK_EQ(wp->layout_cell->g.xoff, xoff);
		CHECK_EQ(wp->sx, sx[i]);
		CHECK_EQ(wp->xoff, xoff);
		xoff += sx[i++] + 1;
	}
}

TEST(layout, spread_out_equalises_siblings)
{
	struct window	*w = make_window(80, 24, 3);

	check_columns(w, (const u_int[]){ 40, 19, 19 });
	layout_spread_out(TAILQ_FIRST(&w->panes));
	check_columns(w, (const u_int[]){ 26, 26, 26 });
	CHECK_EQ(layout_spread_cell(w, w->layout_root), 0);
	drop_window(w);

	w = make_window(81, 24, 3);
	check_columns(w, (const u_int[]){ 40, 20, 19 });
	layout_spread_out(TAILQ_FIRST(&w->panes));
	check_columns(w, (const u_int[]){ 27, 26, 26 });
	drop_window(w);
}

TEST(layout, floating_pane_geometry_resize_and_close)
{
	struct window		*w = make_window(80, 24, 1);
	struct window_pane	*wp1 = TAILQ_FIRST(&w->panes), *wp2;
	struct layout_cell	*lc, *root;
	struct layout_geometry	 lg = { 20, 5, 10, 3 };
	char			*cause = nullptr;

	wp2 = window_add_pane(w, nullptr, 0, SPAWN_FLOATING);
	lc = layout_floating_pane(w, nullptr, &lg);
	layout_assign_pane(lc, wp2, 0);
	root = w->layout_root;

	CHECK(lc->flags & LAYOUT_CELL_FLOATING);
	CHECK(!layout_cell_is_tiled(lc));
	CHECK(window_pane_is_floating(wp2));
	CHECK_EQ(root->type, LAYOUT_TOPBOTTOM);
	CHECK_EQ(layout_count_cells(root), 2u);
	CHECK(lc->parent == root);
	CHECK_EQ(wp2->xoff, 10);
	CHECK_EQ(wp2->yoff, 3);
	CHECK_EQ(wp2->sx, 20u);
	CHECK_EQ(wp2->sy, 5u);
	CHECK_EQ(wp1->xoff, 0);
	CHECK_EQ(wp1->yoff, 0);
	CHECK_EQ(wp1->sx, 80u);
	CHECK_EQ(wp1->sy, 24u);

	CHECK_EQ(layout_resize_floating_pane(wp2, LAYOUT_LEFTRIGHT, 5, 0,
	    &cause), 0);
	CHECK_EQ(lc->g.sx, 25u);
	CHECK_EQ(lc->g.xoff, 10);
	CHECK_EQ(layout_resize_floating_pane(wp2, LAYOUT_TOPBOTTOM, 2, 1,
	    &cause), 0);
	CHECK_EQ(lc->g.sy, 7u);
	CHECK_EQ(lc->g.yoff, 1);

	/* An absolute size includes the two border columns, a change does not. */
	CHECK_EQ(layout_resize_floating_pane_to(wp2, LAYOUT_LEFTRIGHT, 30,
	    &cause), 0);
	CHECK_EQ(lc->g.sx, 28u);

	CHECK_EQ(layout_resize_floating_pane_to(wp2, LAYOUT_LEFTRIGHT, 0,
	    &cause), -1);
	CHECK_EQ(cause, "size is too big or too small");
	free(cause);
	cause = nullptr;
	CHECK_EQ(layout_resize_floating_pane(wp1, LAYOUT_LEFTRIGHT, 5, 0,
	    &cause), -1);
	CHECK_EQ(cause, "pane is not floating");
	free(cause);
	cause = nullptr;
	CHECK_EQ(lc->g.sx, 28u);

	layout_resize(w, 40, 10);
	CHECK_EQ(root->g.sx, 40u);
	CHECK_EQ(root->g.sy, 10u);
	CHECK_EQ(wp1->sx, 40u);
	CHECK_EQ(wp1->sy, 10u);
	CHECK_EQ(lc->g.sx, 28u);
	CHECK_EQ(lc->g.sy, 7u);
	CHECK_EQ(lc->g.xoff, 10);
	CHECK_EQ(lc->g.yoff, 1);
	CHECK_EQ(wp2->sx, 28u);
	CHECK_EQ(wp2->sy, 7u);
	CHECK_EQ(wp2->yoff, 1);

	layout_close_pane(wp2);
	CHECK_NULL(wp2->layout_cell);
	CHECK_EQ(w->layout_root->type, LAYOUT_WINDOWPANE);
	CHECK(w->layout_root->wp == wp1);
	CHECK_EQ(w->layout_root->g.sx, 40u);
	CHECK_EQ(w->layout_root->g.sy, 10u);
	drop_window(w);
}

TEST(layout, split_floating_cell_places_new_cell_beside_or_shares_space)
{
	struct window		*w = make_window(80, 24, 1);
	struct layout_cell	*lc;
	struct layout_geometry	 lg = { 20, 5, 10, 3 }, out;
	char			*cause = nullptr;

	lc = layout_floating_pane(w, nullptr, &lg);
	CHECK_EQ(layout_split_floating_cell(lc, w, &out, PANE_LINES_SINGLE,
	    SPAWN_HORIZONTAL, &cause), 0);
	CHECK_EQ(out.sx, 20u);
	CHECK_EQ(out.sy, 5u);
	CHECK_EQ(out.xoff, 32);
	CHECK_EQ(out.yoff, 3);
	CHECK_EQ(lc->g.sx, 20u);
	CHECK_EQ(lc->g.sy, 5u);
	CHECK_EQ(lc->g.xoff, 10);
	CHECK_EQ(lc->g.yoff, 3);

	CHECK_EQ(layout_split_floating_cell(lc, w, &out, PANE_LINES_SINGLE,
	    SPAWN_HORIZONTAL|SPAWN_BEFORE, &cause), 0);
	CHECK_EQ(out.sx, 12u);
	CHECK_EQ(out.sy, 5u);
	CHECK_EQ(out.xoff, 4);
	CHECK_EQ(out.yoff, 3);
	CHECK_EQ(lc->g.sx, 12u);
	CHECK_EQ(lc->g.sy, 5u);
	CHECK_EQ(lc->g.xoff, 18);
	CHECK_EQ(lc->g.yoff, 3);
	CHECK_NULL(cause);
	drop_window(w);
}

TEST(layout, floating_args_parse_defaults_and_cascades)
{
	struct window		*w = make_window(80, 24, 1);
	struct args		*args = args_create();
	struct layout_geometry	 lg = { UINT_MAX, UINT_MAX, INT_MAX, INT_MAX };
	char			*cause = nullptr;

	CHECK_EQ(layout_floating_args_parse(nullptr, args, PANE_LINES_NONE, w,
	    &lg, &cause), 0);
	CHECK_EQ(lg.sx, 40u);
	CHECK_EQ(lg.sy, 6u);
	CHECK_EQ(lg.xoff, 4);
	CHECK_EQ(lg.yoff, 2);
	CHECK_EQ(w->last_new_pane_x, 4u);
	CHECK_EQ(w->last_new_pane_y, 2u);

	lg = (struct layout_geometry){ UINT_MAX, UINT_MAX, INT_MAX, INT_MAX };
	CHECK_EQ(layout_floating_args_parse(nullptr, args, PANE_LINES_NONE, w,
	    &lg, &cause), 0);
	CHECK_EQ(lg.sx, 40u);
	CHECK_EQ(lg.sy, 6u);
	CHECK_EQ(lg.xoff, 8);
	CHECK_EQ(lg.yoff, 4);
	CHECK_EQ(w->last_new_pane_x, 8u);
	CHECK_EQ(w->last_new_pane_y, 4u);
	CHECK_NULL(cause);

	args_free(args);
	drop_window(w);
}

TEST(layout, cell_get_neighbour_prefers_next_and_skips_floating)
{
	struct window		*w = make_window(80, 24, 3);
	struct window_pane	*wp = TAILQ_FIRST(&w->panes);
	struct layout_cell	*a = wp->layout_cell, *b, *c, *f;
	struct layout_geometry	 lg = { 20, 5, 10, 3 };

	b = TAILQ_NEXT(a, entry);
	c = TAILQ_NEXT(b, entry);
	CHECK(layout_cell_get_neighbour(a) == b);
	CHECK(layout_cell_get_neighbour(b) == c);
	CHECK(layout_cell_get_neighbour(c) == b);
	CHECK_NULL(layout_cell_get_neighbour(w->layout_root));

	f = layout_floating_pane(w, wp, &lg);
	CHECK(TAILQ_NEXT(a, entry) == f);
	CHECK(layout_cell_get_neighbour(a) == b);
	CHECK(layout_cell_get_neighbour(f) == b);
	CHECK(layout_cell_get_neighbour(c) == b);
	drop_window(w);
}

TEST(layout, search_by_border_and_border_status_offsets)
{
	struct window		*w = make_window(80, 24, 2);
	struct layout_cell	*root = w->layout_root, *a;
	struct window_pane	*wp;

	a = TAILQ_FIRST(&root->cells);
	CHECK(layout_search_by_border(root, 40, 5) == a);
	CHECK_NULL(layout_search_by_border(root, 5, 5));
	CHECK_NULL(layout_search_by_border(root, 200, 5));

	CHECK_EQ(layout_set_select(w, 1), 1u);
	root = w->layout_root;
	a = TAILQ_FIRST(&root->cells);
	CHECK_EQ(a->g.sy, 12u);
	CHECK(layout_search_by_border(root, 5, 12) == a);
	CHECK_NULL(layout_search_by_border(root, 5, 13));
	drop_window(w);

	options_set_number(global_w_options, "pane-border-status",
	    PANE_STATUS_TOP);
	w = make_window(80, 24, 1);
	wp = TAILQ_FIRST(&w->panes);
	CHECK_EQ(w->layout_root->g.sy, 24u);
	CHECK_EQ(wp->yoff, 1);
	CHECK_EQ(wp->sy, 23u);
	drop_window(w);

	options_set_number(global_w_options, "pane-border-status",
	    PANE_STATUS_BOTTOM);
	w = make_window(80, 24, 1);
	wp = TAILQ_FIRST(&w->panes);
	CHECK_EQ(wp->yoff, 0);
	CHECK_EQ(wp->sy, 23u);
	drop_window(w);
}
