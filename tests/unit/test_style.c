#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

static struct style
fresh(void)
{
	struct style	sy;

	style_set(&sy, &grid_default_cell);
	return (sy);
}

TEST(style, set_starts_from_base_cell)
{
	struct style	sy = fresh();

	CHECK(grid_cells_equal(&sy.gc, &grid_default_cell));
	CHECK_EQ(sy.align, STYLE_ALIGN_DEFAULT);
	CHECK_EQ(sy.list, STYLE_LIST_OFF);
	CHECK_EQ(sy.range_type, STYLE_RANGE_NONE);
	CHECK_EQ(sy.fill, 8);
}

TEST(style, parse_colours_and_attributes)
{
	struct style	sy = fresh();

	CHECK_EQ(style_parse(&sy, &grid_default_cell,
	    "fg=red,bg=#0000ff,us=colour5,bold,underscore,italics"), 0);
	CHECK_EQ(sy.gc.fg, 1);
	CHECK_EQ(sy.gc.bg, 0x0000ff | COLOUR_FLAG_RGB);
	CHECK_EQ(sy.gc.us, 5 | COLOUR_FLAG_256);
	CHECK(sy.gc.attr & GRID_ATTR_BRIGHT);
	CHECK(sy.gc.attr & GRID_ATTR_UNDERSCORE);
	CHECK(sy.gc.attr & GRID_ATTR_ITALICS);

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "nobold nounderscore"), 0);
	CHECK_EQ(sy.gc.attr & GRID_ATTR_BRIGHT, 0);
	CHECK_EQ(sy.gc.attr & GRID_ATTR_UNDERSCORE, 0);
	CHECK(sy.gc.attr & GRID_ATTR_ITALICS);

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "none"), 0);
	CHECK_EQ(sy.gc.attr, 0);
}

TEST(style, parse_layout_keywords)
{
	struct style	sy = fresh();

	CHECK_EQ(style_parse(&sy, &grid_default_cell,
	    "align=right,fill=colour123,list=focus,range=window|3"), 0);
	CHECK_EQ(sy.align, STYLE_ALIGN_RIGHT);
	CHECK_EQ(sy.fill, 123 | COLOUR_FLAG_256);
	CHECK_EQ(sy.list, STYLE_LIST_FOCUS);
	CHECK_EQ(sy.range_type, STYLE_RANGE_WINDOW);
	CHECK_EQ(sy.range_argument, 3u);

	CHECK_EQ(style_parse(&sy, &grid_default_cell,
	    "align=centre,nolist,norange,range=left"), 0);
	CHECK_EQ(sy.align, STYLE_ALIGN_CENTRE);
	CHECK_EQ(sy.list, STYLE_LIST_OFF);
	CHECK_EQ(sy.range_type, STYLE_RANGE_LEFT);

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "range=user|foo"), 0);
	CHECK_EQ(sy.range_type, STYLE_RANGE_USER);
	CHECK_EQ(sy.range_string, "foo");

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "push-default"), 0);
	CHECK_EQ(sy.default_type, STYLE_DEFAULT_PUSH);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "pop-default"), 0);
	CHECK_EQ(sy.default_type, STYLE_DEFAULT_POP);
}

TEST(style, default_keyword_restores_base)
{
	struct style		sy = fresh();
	struct grid_cell	base;

	memcpy(&base, &grid_default_cell, sizeof base);
	base.fg = 4;
	CHECK_EQ(style_parse(&sy, &base, "fg=red,bold"), 0);
	CHECK_EQ(sy.gc.fg, 1);
	CHECK_EQ(style_parse(&sy, &base, "default"), 0);
	CHECK_EQ(sy.gc.fg, 4);
	CHECK_EQ(sy.gc.attr, 0);
}

TEST(style, bad_input_leaves_style_untouched)
{
	struct style	sy = fresh(), before;

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "fg=red,align=right"), 0);
	style_copy(&before, &sy);

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "fg=blue,bg=nonsense"), -1);
	CHECK_EQ(sy.gc.fg, before.gc.fg);
	CHECK_EQ(sy.gc.bg, before.gc.bg);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "align=sideways"), -1);
	CHECK_EQ(sy.align, STYLE_ALIGN_RIGHT);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "list=maybe"), -1);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "range=window|x"), -1);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "whatever"), -1);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, ""), 0);
}

TEST(style, tostring_reflects_state)
{
	struct style	sy = fresh();
	const char	*s;

	CHECK_EQ(style_parse(&sy, &grid_default_cell,
	    "fg=red,bg=blue,bold,align=right,list=on"), 0);
	s = style_tostring(&sy);
	CHECK_NONNULL(strstr(s, "fg=red"));
	CHECK_NONNULL(strstr(s, "bg=blue"));
	CHECK_NONNULL(strstr(s, "bright"));
	CHECK_NONNULL(strstr(s, "align=right"));
	CHECK_NONNULL(strstr(s, "list=on"));

	sy = fresh();
	CHECK_EQ(style_tostring(&sy), "default");
}

TEST(style, tostring_parses_back)
{
	struct style	a = fresh(), b = fresh();
	char		*s;

	CHECK_EQ(style_parse(&a, &grid_default_cell,
	    "fg=#123456,bg=colour200,us=green,underscore,reverse,fill=red,"
	    "align=centre,range=session|$4"), 0);
	s = xstrdup(style_tostring(&a));
	CHECK_EQ(style_parse(&b, &grid_default_cell, s), 0);
	CHECK(grid_cells_equal(&a.gc, &b.gc));
	CHECK_EQ(b.fill, a.fill);
	CHECK_EQ(b.align, a.align);
	CHECK_EQ(b.range_type, a.range_type);
	CHECK_EQ(b.range_argument, a.range_argument);
	free(s);
}

TEST(style, parse_every_attribute_name_and_negation)
{
	static const struct {
		const char	*name;
		int		 bit;
	} table[] = {
		{ "acs", GRID_ATTR_CHARSET },
		{ "bright", GRID_ATTR_BRIGHT },
		{ "bold", GRID_ATTR_BRIGHT },
		{ "dim", GRID_ATTR_DIM },
		{ "underscore", GRID_ATTR_UNDERSCORE },
		{ "blink", GRID_ATTR_BLINK },
		{ "reverse", GRID_ATTR_REVERSE },
		{ "hidden", GRID_ATTR_HIDDEN },
		{ "italics", GRID_ATTR_ITALICS },
		{ "strikethrough", GRID_ATTR_STRIKETHROUGH },
		{ "double-underscore", GRID_ATTR_UNDERSCORE_2 },
		{ "curly-underscore", GRID_ATTR_UNDERSCORE_3 },
		{ "dotted-underscore", GRID_ATTR_UNDERSCORE_4 },
		{ "dashed-underscore", GRID_ATTR_UNDERSCORE_5 },
		{ "overline", GRID_ATTR_OVERLINE }
	};
	const int	all = GRID_ATTR_NOATTR - 1;
	struct style	sy;
	char		no[32];
	u_int		i;

	for (i = 0; i < nitems(table); i++) {
		sy = fresh();
		CHECK_EQ(style_parse(&sy, &grid_default_cell, table[i].name), 0);
		CHECK_EQ(sy.gc.attr, table[i].bit);

		sy.gc.attr = all;
		xsnprintf(no, sizeof no, "no%s", table[i].name);
		CHECK_EQ(style_parse(&sy, &grid_default_cell, no), 0);
		CHECK_EQ(sy.gc.attr, all & ~table[i].bit);
	}

	sy = fresh();
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "BOLD"), 0);
	CHECK_EQ(sy.gc.attr, GRID_ATTR_BRIGHT);
	sy = fresh();
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "bold|dim"), 0);
	CHECK_EQ(sy.gc.attr, GRID_ATTR_BRIGHT|GRID_ATTR_DIM);
	sy = fresh();
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "noattr"), 0);
	CHECK_EQ(sy.gc.attr, GRID_ATTR_NOATTR);

	CHECK_EQ(attributes_fromstring(attributes_tostring(all)), all);
	/* attributes_tostring emits noattr but only style_parse accepts it. */
	CHECK_EQ(attributes_fromstring(attributes_tostring(GRID_ATTR_NOATTR)),
	    -1);
}

TEST(style, apply_and_add_overlay_option_onto_cell)
{
	struct grid_cell	 gc, before;
	struct style		*sy;

	options_set_string(global_w_options, "window-style", 0, "%s",
	    "fg=red,bg=#112233,us=colour5,bold");
	style_apply(&gc, global_w_options, "window-style", nullptr);
	CHECK_EQ(gc.fg, 1);
	CHECK_EQ(gc.bg, 0x112233 | COLOUR_FLAG_RGB);
	CHECK_EQ(gc.us, 5 | COLOUR_FLAG_256);
	CHECK_EQ(gc.attr, GRID_ATTR_BRIGHT);
	CHECK_EQ(gc.data.data[0], ' ');

	gc.fg = 4;
	gc.bg = 2;
	gc.attr = GRID_ATTR_DIM;
	options_set_string(global_w_options, "window-style", 0, "%s",
	    "bg=yellow,italics");
	sy = style_add(&gc, global_w_options, "window-style", nullptr);
	CHECK_EQ(sy->gc.bg, 3);
	CHECK_EQ(gc.fg, 4);
	CHECK_EQ(gc.bg, 3);
	CHECK_EQ(gc.attr, GRID_ATTR_DIM|GRID_ATTR_ITALICS);

	memcpy(&before, &gc, sizeof before);
	options_set_string(global_w_options, "window-style", 0, "%s", "");
	style_add(&gc, global_w_options, "window-style", nullptr);
	CHECK(grid_cells_equal(&gc, &before));

	style_apply(&gc, global_w_options, "window-active-style", nullptr);
	CHECK(grid_cells_equal(&gc, &grid_default_cell));
}

TEST(style, parse_colour_single_value)
{
	struct style		sy;
	struct grid_cell	base;

	memcpy(&base, &grid_default_cell, sizeof base);
	base.fg = 4;

	CHECK_EQ(style_parse_colour(&sy, &base, "red"), 0);
	CHECK_EQ(sy.gc.fg, 1);
	/* A failed parse has already reset the style to base. */
	CHECK_EQ(style_parse_colour(&sy, &base, "nonsense"), -1);
	CHECK_EQ(sy.gc.fg, 4);
	CHECK_EQ(style_parse_colour(&sy, &base, ""), 0);
	CHECK_EQ(sy.gc.fg, -1);
	CHECK_EQ(style_parse_colour(&sy, &base, "default"), 0);
	CHECK_EQ(sy.gc.fg, 4);
}

TEST(style, link_nolink_and_default_reset)
{
	struct style	sy = fresh();
	u_int		id;

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "link=https://x.test"), 0);
	CHECK(sy.link != 0);
	CHECK_EQ(style_link(&sy), "https://x.test");
	CHECK_NONNULL(strstr(style_tostring(&sy), "link=https://x.test"));
	id = sy.link;

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "nolink"), 0);
	CHECK_EQ(sy.link, 0u);
	CHECK_NULL(style_link(&sy));

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "link=https://x.test"), 0);
	CHECK_EQ(sy.link, id);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "link="), 0);
	CHECK_EQ(sy.link, 0u);

	CHECK_EQ(style_parse(&sy, &grid_default_cell,
	    "link=https://x.test,default"), 0);
	CHECK_EQ(sy.link, 0u);
	CHECK_NULL(style_link(&sy));
}

TEST(style, dim_width_pad_align_list_range_forms_and_bad_input_atomicity)
{
	static const char	*bad[] = { "dim=101", "range=pane|7",
	    "range=control|10", "width=abc", "fill=nonsense", "range=user",
	    "fg=red,bogus", nullptr };
	struct style		 sy = fresh(), saved;
	const char		*s;
	char			 big[301];
	u_int			 i;

	CHECK_EQ(style_parse(&sy, &grid_default_cell, "dim=30"), 0);
	CHECK_EQ(sy.dim, 30);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "width=10"), 0);
	CHECK_EQ(sy.width, 10);
	CHECK_EQ(sy.width_percentage, 0);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "width=25%"), 0);
	CHECK_EQ(sy.width, 25);
	CHECK_EQ(sy.width_percentage, 1);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "pad=2"), 0);
	CHECK_EQ(sy.pad, 2);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "align=absolute-centre"),
	    0);
	CHECK_EQ(sy.align, STYLE_ALIGN_ABSOLUTE_CENTRE);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "list=left-marker"), 0);
	CHECK_EQ(sy.list, STYLE_LIST_LEFT_MARKER);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "range=control|3"), 0);
	CHECK_EQ(sy.range_type, STYLE_RANGE_CONTROL);
	CHECK_EQ(sy.range_argument, 3u);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "range=session|$2"), 0);
	CHECK_EQ(sy.range_type, STYLE_RANGE_SESSION);
	CHECK_EQ(sy.range_argument, 2u);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "range=pane|%7"), 0);
	CHECK_EQ(sy.range_type, STYLE_RANGE_PANE);
	CHECK_EQ(sy.range_argument, 7u);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "set-default"), 0);
	CHECK_EQ(sy.default_type, STYLE_DEFAULT_SET);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "ignore"), 0);
	CHECK_EQ(sy.ignore, 1);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "noignore"), 0);
	CHECK_EQ(sy.ignore, 0);
	CHECK_EQ(style_parse(&sy, &grid_default_cell, "dim=50%"), 0);
	CHECK_EQ(sy.dim, 50);

	s = style_tostring(&sy);
	CHECK_NONNULL(strstr(s, "dim=50%"));
	CHECK_NONNULL(strstr(s, "width=25%"));
	CHECK_NONNULL(strstr(s, "pad=2"));
	CHECK_NONNULL(strstr(s, "range=pane|%7"));

	style_copy(&saved, &sy);
	for (i = 0; bad[i] != nullptr; i++) {
		CHECK_EQ(style_parse(&sy, &grid_default_cell, bad[i]), -1);
		CHECK_EQ(memcmp(&sy, &saved, sizeof sy), 0);
	}
	memset(big, 'a', sizeof big - 1);
	big[sizeof big - 1] = '\0';
	CHECK_EQ(style_parse(&sy, &grid_default_cell, big), -1);
	CHECK_EQ(memcmp(&sy, &saved, sizeof sy), 0);
}

TEST(style, ranges_get_range_by_position)
{
	struct style_ranges	 srs;
	struct style_range	*a, *b;

	style_ranges_init(&srs);
	a = xcalloc(1, sizeof *a);
	a->start = 0;
	a->end = 3;
	TAILQ_INSERT_TAIL(&srs, a, entry);
	b = xcalloc(1, sizeof *b);
	b->start = 5;
	b->end = 8;
	TAILQ_INSERT_TAIL(&srs, b, entry);

	CHECK(style_ranges_get_range(&srs, 2) == a);
	CHECK_NULL(style_ranges_get_range(&srs, 3));
	CHECK(style_ranges_get_range(&srs, 5) == b);
	CHECK(style_ranges_get_range(&srs, 7) == b);
	CHECK_NULL(style_ranges_get_range(&srs, 9));
	CHECK_NULL(style_ranges_get_range(nullptr, 0));

	style_ranges_free(&srs);
	CHECK(TAILQ_EMPTY(&srs));
}

TEST(style, scrollbar_style_from_option_defaults_and_override)
{
	struct style	sb;

	style_set_scrollbar_style_from_option(&sb, global_w_options);
	CHECK_EQ(sb.width, PANE_SCROLLBARS_DEFAULT_WIDTH);
	CHECK_EQ(sb.pad, PANE_SCROLLBARS_DEFAULT_PADDING);
	CHECK_EQ(sb.gc.data.data[0], PANE_SCROLLBARS_CHARACTER);
	CHECK_EQ(sb.gc.fg, COLOUR_THEME_LIGHT_GREY | COLOUR_FLAG_THEME);
	CHECK_EQ(sb.gc.bg, COLOUR_THEME_DARK_GREY | COLOUR_FLAG_THEME);

	options_set_string(global_w_options, "pane-scrollbars-style", 0, "%s",
	    "fg=red,width=3,pad=1");
	style_set_scrollbar_style_from_option(&sb, global_w_options);
	CHECK_EQ(sb.gc.fg, 1);
	CHECK_EQ(sb.gc.bg, COLOUR_THEME_DARK_GREY | COLOUR_FLAG_THEME);
	CHECK_EQ(sb.width, 3);
	CHECK_EQ(sb.pad, 1);

	options_set_string(global_w_options, "pane-scrollbars-style", 0, "%s",
	    "width=zz");
	style_set_scrollbar_style_from_option(&sb, global_w_options);
	CHECK_EQ(sb.gc.fg, COLOUR_THEME_LIGHT_GREY | COLOUR_FLAG_THEME);
	CHECK_EQ(sb.width, PANE_SCROLLBARS_DEFAULT_WIDTH);
	CHECK_EQ(sb.pad, PANE_SCROLLBARS_DEFAULT_PADDING);
}
