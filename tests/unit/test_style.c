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
