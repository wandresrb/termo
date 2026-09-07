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
