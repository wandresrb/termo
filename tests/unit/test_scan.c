#include <sys/types.h>

#include <limits.h>
#include <string.h>

#include "termo.h"
#include "test.h"

TEST(scan, literal_consumes_only_on_match)
{
	const char	*p = "rgb:ff";

	CHECK(scan_lit(&p, "rgb:"));
	CHECK_EQ(p, "ff");
	CHECK(!scan_lit(&p, "rgb:"));
	CHECK_EQ(p, "ff");
	CHECK(scan_lit(&p, ""));
}

TEST(scan, unsigned_decimal_is_digits_only_and_bounded)
{
	const char	*p;
	u_int		 v = 7;

	p = "80x24";
	CHECK(scan_u(&p, &v, UINT_MAX));
	CHECK_EQ(v, 80u);
	CHECK_EQ(p, "x24");

	p = "x24";
	CHECK(!scan_u(&p, &v, UINT_MAX));
	CHECK_EQ(p, "x24");
	p = " 24";
	CHECK(!scan_u(&p, &v, UINT_MAX));
	p = "-1";
	CHECK(!scan_u(&p, &v, UINT_MAX));
	p = "+1";
	CHECK(!scan_u(&p, &v, UINT_MAX));

	p = "256";
	CHECK(!scan_u(&p, &v, 255));
	CHECK_EQ(p, "256");
	p = "255";
	CHECK(scan_u(&p, &v, 255));

	p = "4294967295";
	CHECK(scan_u(&p, &v, UINT_MAX));
	CHECK_EQ(v, UINT_MAX);
	p = "4294967296";
	CHECK(!scan_u(&p, &v, UINT_MAX));
	p = "99999999999999999999";
	CHECK(!scan_u(&p, &v, UINT_MAX));
}

TEST(scan, signed_decimal_takes_a_minus_and_a_range)
{
	const char	*p;
	int		 v = 7;

	p = "-12,";
	CHECK(scan_i(&p, &v, INT_MIN, INT_MAX));
	CHECK_EQ(v, -12);
	CHECK_EQ(p, ",");
	p = "-12";
	CHECK(!scan_i(&p, &v, 0, INT_MAX));
	CHECK_EQ(p, "-12");
	p = "-";
	CHECK(!scan_i(&p, &v, INT_MIN, INT_MAX));
	p = "2147483647";
	CHECK(scan_i(&p, &v, INT_MIN, INT_MAX));
	CHECK_EQ(v, INT_MAX);
	p = "2147483648";
	CHECK(!scan_i(&p, &v, INT_MIN, INT_MAX));
}

TEST(scan, hex_fixed_width_and_free_width)
{
	const char	*p;
	u_int		 v = 7;

	p = "ff8000";
	CHECK(scan_x(&p, &v, 2));
	CHECK_EQ(v, 0xffu);
	CHECK(scan_x(&p, &v, 2));
	CHECK_EQ(v, 0x80u);
	CHECK(scan_x(&p, &v, 2));
	CHECK_EQ(v, 0u);
	CHECK_EQ(*p, '\0');

	p = "f/";
	CHECK(!scan_x(&p, &v, 2));
	CHECK_EQ(p, "f/");
	p = "ABCD,";
	CHECK(scan_x(&p, &v, 4));
	CHECK_EQ(v, 0xabcdu);
	CHECK_EQ(p, ",");

	p = "1b";
	CHECK(scan_x(&p, &v, 0));
	CHECK_EQ(v, 0x1bu);
	CHECK_EQ(*p, '\0');
	p = "zz";
	CHECK(!scan_x(&p, &v, 0));
	p = "ffffffff";
	CHECK(scan_x(&p, &v, 0));
	CHECK_EQ(v, UINT_MAX);
	p = "100000000";
	CHECK(!scan_x(&p, &v, 0));
	CHECK_EQ(p, "100000000");
}

TEST(scan, double_uses_strtod_and_advances)
{
	const char	*p = "0.5/1";
	double		 v = 7;

	CHECK(scan_d(&p, &v));
	CHECK(v == 0.5);
	CHECK_EQ(p, "/1");
	p = "/1";
	CHECK(!scan_d(&p, &v));
	CHECK_EQ(p, "/1");
}

TEST(scan, a_format_is_a_chain)
{
	const char	*p = "@3:80x24";
	u_int		 w = 0, x = 0, y = 0;

	CHECK(scan_lit(&p, "@") && scan_u(&p, &w, UINT_MAX) &&
	    scan_lit(&p, ":") && scan_u(&p, &x, UINT_MAX) &&
	    scan_lit(&p, "x") && scan_u(&p, &y, UINT_MAX) && *p == '\0');
	CHECK_EQ(w, 3u);
	CHECK_EQ(x, 80u);
	CHECK_EQ(y, 24u);

	p = "@3:80x24junk";
	CHECK(!(scan_lit(&p, "@") && scan_u(&p, &w, UINT_MAX) &&
	    scan_lit(&p, ":") && scan_u(&p, &x, UINT_MAX) &&
	    scan_lit(&p, "x") && scan_u(&p, &y, UINT_MAX) && *p == '\0'));
}
