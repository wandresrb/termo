#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

TEST(colour, fromstring_names_and_numbers)
{
	CHECK_EQ(colour_fromstring("black"), 0);
	CHECK_EQ(colour_fromstring("red"), 1);
	CHECK_EQ(colour_fromstring("WHITE"), 7);
	CHECK_EQ(colour_fromstring("brightred"), 91);
	CHECK_EQ(colour_fromstring("97"), 97);
	CHECK_EQ(colour_fromstring("4"), 4);
	CHECK_EQ(colour_fromstring("default"), 8);
	CHECK_EQ(colour_fromstring("terminal"), 9);
}

TEST(colour, fromstring_256_and_rgb)
{
	CHECK_EQ(colour_fromstring("colour123"), 123 | COLOUR_FLAG_256);
	CHECK_EQ(colour_fromstring("color5"), 5 | COLOUR_FLAG_256);
	CHECK_EQ(colour_fromstring("COLOUR0"), COLOUR_FLAG_256);
	CHECK_EQ(colour_fromstring("colour256"), -1);
	CHECK_EQ(colour_fromstring("colour"), -1);
	CHECK_EQ(colour_fromstring("#ff0000"), 0xff0000 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_fromstring("#0A0b0C"), 0x0a0b0c | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_fromstring("#ff00"), -1);
	CHECK_EQ(colour_fromstring("#gg0000"), -1);
	CHECK_EQ(colour_fromstring("#ff00000"), -1);
}

TEST(colour, fromstring_x11_and_junk)
{
	CHECK_EQ(colour_fromstring("orange"), 0xffa500 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_byname("AliceBlue"), 0xf0f8ff | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_fromstring("notacolour"), -1);
	CHECK_EQ(colour_fromstring(""), -1);
	CHECK_EQ(colour_fromstring("256"), -1);
}

TEST(colour, tostring_roundtrips)
{
	static const char	*names[] = { "black", "red", "green", "yellow",
	    "blue", "magenta", "cyan", "white", "brightblack", "brightwhite",
	    "default", "terminal", "colour123", "#ff8000", nullptr };
	u_int			 i;

	for (i = 0; names[i] != nullptr; i++)
		CHECK_EQ(colour_tostring(colour_fromstring(names[i])), names[i]);
	CHECK_EQ(colour_tostring(-1), "none");
}

TEST(colour, rgb_join_and_split)
{
	u_char	r = 0, g = 0, b = 0;
	int	c = colour_join_rgb(0x12, 0x34, 0x56);

	CHECK(c & COLOUR_FLAG_RGB);
	CHECK_EQ(c & 0xffffff, 0x123456);
	colour_split_rgb(c, &r, &g, &b);
	CHECK_EQ(r, 0x12);
	CHECK_EQ(g, 0x34);
	CHECK_EQ(b, 0x56);
}

TEST(colour, find_rgb_picks_nearest_cube_entry)
{
	CHECK_EQ(colour_find_rgb(0, 0, 0), 16 | COLOUR_FLAG_256);
	CHECK_EQ(colour_find_rgb(255, 255, 255), 231 | COLOUR_FLAG_256);
	CHECK_EQ(colour_find_rgb(0x5f, 0x87, 0xaf), 67 | COLOUR_FLAG_256);
	CHECK_EQ(colour_find_rgb(0x80, 0x80, 0x80), 244 | COLOUR_FLAG_256);
}

TEST(colour, force_rgb_and_256to16)
{
	int	c = colour_force_rgb(1);

	CHECK(c & COLOUR_FLAG_RGB);
	CHECK_EQ(colour_force_rgb(8), -1);
	CHECK_EQ(colour_force_rgb(9), -1);
	CHECK_EQ(colour_force_rgb(0xff0000 | COLOUR_FLAG_RGB), 0xff0000 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_256to16(1), 1);
	CHECK_EQ(colour_256to16(15), 15);
	CHECK_EQ(colour_256to16(16), 0);
	CHECK_EQ(colour_256to16(231), 15);
}

TEST(colour, dim_leaves_defaults_alone)
{
	u_char	r, g, b;
	int	c = colour_join_rgb(200, 100, 50), d;

	CHECK_EQ(colour_dim(c, 0), c);
	CHECK_EQ(colour_dim(8, 50), 8);
	CHECK_EQ(colour_dim(9, 50), 9);
	d = colour_dim(c, 50);
	CHECK(d & COLOUR_FLAG_RGB);
	colour_split_rgb(d, &r, &g, &b);
	CHECK(r < 200);
	CHECK(g < 100);
	CHECK(b < 50);
}

TEST(colour, palette_set_get_clear)
{
	struct colour_palette	p;
	int			rgb = colour_join_rgb(1, 2, 3);

	colour_palette_init(&p);
	CHECK_EQ(colour_palette_get(&p, 1), -1);
	CHECK_EQ(colour_palette_set(&p, 1, rgb), 1);
	CHECK_EQ(colour_palette_get(&p, 1), rgb);
	CHECK_EQ(colour_palette_set(&p, 9, rgb), 1);
	CHECK_EQ(colour_palette_get(&p, 91), rgb);
	CHECK_EQ(colour_palette_set(&p, 256, rgb), 0);
	CHECK_EQ(colour_palette_set(&p, -1, rgb), 0);
	CHECK_EQ(colour_palette_get(nullptr, 1), -1);
	colour_palette_clear(&p);
	CHECK_EQ(colour_palette_get(&p, 1), -1);
	colour_palette_free(&p);
}

TEST(colour, parseX11_accepts_every_documented_form)
{
	int	orange = 0xff8000 | COLOUR_FLAG_RGB;

	CHECK_EQ(colour_parseX11("rgb:ff/80/00"), orange);
	CHECK_EQ(colour_parseX11("rgb:ffff/8000/0000"), orange);
	CHECK_EQ(colour_parseX11("#ff8000"), orange);
	CHECK_EQ(colour_parseX11("#ffff80000000"), orange);
	CHECK_EQ(colour_parseX11("255,128,0"), orange);
	CHECK_EQ(colour_parseX11("cmyk:0/0.5/1/0"), 0xff7f00 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_parseX11("cmy:0/0.5/1"), 0xff7f00 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_parseX11("cmyk:2/0/0/0"), -1);
	/* partial matches leave targets unwritten: no branch may read them */
	CHECK_EQ(colour_parseX11("cmyk:0.5/0.5"), -1);
	CHECK_EQ(colour_parseX11("cmyk:0/0/0"), -1);
	CHECK_EQ(colour_parseX11("cmy:0/0"), -1);
	CHECK_EQ(colour_parseX11("AliceBlue"), 0xf0f8ff | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_parseX11("  AliceBlue  "), 0xf0f8ff | COLOUR_FLAG_RGB);
	/* round(2.55 * 50) is 127: the double product lands under 127.5. */
	CHECK_EQ(colour_parseX11("grey50"), 0x7f7f7f | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_parseX11("gray"), 0xbebebe | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_parseX11("rgb:ff/80"), -1);
	CHECK_EQ(colour_parseX11("notacolour"), -1);
	CHECK_EQ(colour_parseX11(""), -1);
	CHECK_EQ(colour_parseX11("300,0,0"), -1);
	CHECK_EQ(colour_parseX11("0,256,0"), -1);
	CHECK_EQ(colour_parseX11("-1,0,0"), -1);
}

TEST(colour, 256toRGB_table_roundtrips_through_find_rgb)
{
	u_char	r, g, b;
	int	i;

	CHECK_EQ(colour_256toRGB(0), 0x000000 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_256toRGB(9), 0xff0000 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_256toRGB(21), 0x0000ff | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_256toRGB(67), 0x5f87af | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_256toRGB(232), 0x080808 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_256toRGB(255), 0xeeeeee | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_256toRGB(44 | COLOUR_FLAG_256), colour_256toRGB(44));

	for (i = 16; i < 256; i++) {
		colour_split_rgb(colour_256toRGB(i), &r, &g, &b);
		CHECK_EQ(colour_find_rgb(r, g, b), i | COLOUR_FLAG_256);
	}
}

TEST(colour, palette_from_option_builds_default_palette)
{
	struct colour_palette	 p;
	struct options_entry	*o;
	char			*cause = nullptr;
	int			 rgb = colour_join_rgb(1, 2, 3);

	o = options_get(global_w_options, "pane-colours");
	REQUIRE_NONNULL(o);
	CHECK_EQ(options_array_set(o, "1", "#ff0000", 0, &cause), 0);
	CHECK_EQ(options_array_set(o, "200", "colour5", 0, &cause), 0);
	CHECK_EQ(options_array_set(o, "2", "nope", 0, &cause), -1);
	CHECK_EQ(cause, "bad colour: nope");
	free(cause);

	colour_palette_init(&p);
	colour_palette_from_option(&p, global_w_options);
	CHECK_NONNULL(p.default_palette);
	CHECK_EQ(colour_palette_get(&p, 1), 0xff0000 | COLOUR_FLAG_RGB);
	CHECK_EQ(colour_palette_get(&p, 200 | COLOUR_FLAG_256),
	    5 | COLOUR_FLAG_256);
	CHECK_EQ(colour_palette_get(&p, 3), -1);

	CHECK_EQ(colour_palette_set(&p, 1, rgb), 1);
	CHECK_EQ(colour_palette_get(&p, 1), rgb);
	colour_palette_clear(&p);
	CHECK_EQ(colour_palette_get(&p, 1), 0xff0000 | COLOUR_FLAG_RGB);

	CHECK_EQ(options_array_set(o, "1", nullptr, 0, &cause), 0);
	CHECK_EQ(options_array_set(o, "200", nullptr, 0, &cause), 0);
	colour_palette_from_option(&p, global_w_options);
	CHECK_NULL(p.default_palette);
	CHECK_EQ(colour_palette_get(&p, 1), -1);

	colour_palette_from_option(nullptr, global_w_options);
	colour_palette_free(&p);
}

TEST(colour, totheme_toescape_and_theme_table)
{
	int	c;

	CHECK_EQ(colour_totheme(0), THEME_DARK);
	CHECK_EQ(colour_totheme(7), THEME_LIGHT);
	CHECK_EQ(colour_totheme(90), THEME_DARK);
	CHECK_EQ(colour_totheme(97), THEME_LIGHT);
	CHECK_EQ(colour_totheme(0xffffff | COLOUR_FLAG_RGB), THEME_LIGHT);
	CHECK_EQ(colour_totheme(0x000000 | COLOUR_FLAG_RGB), THEME_DARK);
	CHECK_EQ(colour_totheme(0x808080 | COLOUR_FLAG_RGB), THEME_LIGHT);
	CHECK_EQ(colour_totheme(0x7f7f7f | COLOUR_FLAG_RGB), THEME_DARK);
	CHECK_EQ(colour_totheme(231 | COLOUR_FLAG_256), THEME_LIGHT);
	CHECK_EQ(colour_totheme(16 | COLOUR_FLAG_256), THEME_DARK);
	CHECK_EQ(colour_totheme(-1), THEME_UNKNOWN);
	CHECK_EQ(colour_totheme(8), THEME_UNKNOWN);

	CHECK_EQ(colour_toescape(nullptr, 1, 0), "\033[31m");
	CHECK_EQ(colour_toescape(nullptr, 1, 1), "\033[41m");
	CHECK_EQ(colour_toescape(nullptr, 8, 0), "\033[39m");
	CHECK_EQ(colour_toescape(nullptr, 0xff0000 | COLOUR_FLAG_RGB, 0),
	    "\033[38;2;255;0;0m");
	CHECK_EQ(colour_toescape(nullptr, 200 | COLOUR_FLAG_256, 1),
	    "\033[48;5;200m");
	CHECK_EQ(colour_toescape(nullptr, 91, 0), "\033[91m");
	CHECK_EQ(colour_toescape(nullptr, 91, 1), "\033[101m");

	c = colour_fromstring("thememagenta");
	CHECK(c & COLOUR_FLAG_THEME);
	CHECK_EQ(c & 0xff, COLOUR_THEME_MAGENTA);
	CHECK_EQ(colour_theme_terminal_colour(c & 0xff), 5);
	CHECK_EQ(colour_theme_option(c & 0xff, THEME_LIGHT),
	    "light-theme-magenta");
	CHECK_EQ(colour_theme_option(c & 0xff, THEME_DARK),
	    "dark-theme-magenta");
	CHECK_NULL(colour_theme_option(99, THEME_DARK));
	CHECK_EQ(colour_theme_terminal_colour(99), 8);
	CHECK_EQ(colour_toescape(nullptr, c, 0), "\033[35m");
}
