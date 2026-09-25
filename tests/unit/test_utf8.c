#include <sys/types.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "termo.h"
#include "test.h"

/* Decode one character from s into ud; returns the final decoder state. */
static enum utf8_state
decode(struct utf8_data *ud, const char *s)
{
	enum utf8_state	st;

	if ((u_char)*s < 0x80) {
		utf8_set(ud, *s);
		return (UTF8_DONE);
	}
	st = utf8_open(ud, *s++);
	while (st == UTF8_MORE && *s != '\0')
		st = utf8_append(ud, *s++);
	return (st);
}

TEST(utf8, open_classifies_lead_bytes)
{
	struct utf8_data	ud;

	CHECK_EQ(utf8_open(&ud, 'a'), UTF8_ERROR);
	CHECK_EQ(utf8_open(&ud, 0x80), UTF8_ERROR);
	CHECK_EQ(utf8_open(&ud, 0xc0), UTF8_ERROR);
	CHECK_EQ(utf8_open(&ud, 0xc1), UTF8_ERROR);
	CHECK_EQ(utf8_open(&ud, 0xc2), UTF8_MORE);
	CHECK_EQ(ud.size, 2);
	CHECK_EQ(utf8_open(&ud, 0xe0), UTF8_MORE);
	CHECK_EQ(ud.size, 3);
	CHECK_EQ(utf8_open(&ud, 0xf0), UTF8_MORE);
	CHECK_EQ(ud.size, 4);
	CHECK_EQ(utf8_open(&ud, 0xf4), UTF8_MORE);
	CHECK_EQ(utf8_open(&ud, 0xf5), UTF8_ERROR);
	CHECK_EQ(utf8_open(&ud, 0xff), UTF8_ERROR);
}

TEST(utf8, append_decodes_two_three_four_bytes)
{
	struct utf8_data	ud;

	CHECK_EQ(decode(&ud, "\xc3\xa9"), UTF8_DONE);		/* U+00E9 */
	CHECK_EQ(ud.size, 2);
	CHECK_EQ(ud.have, 2);
	CHECK_EQ(ud.width, 1);

	CHECK_EQ(decode(&ud, "\xe4\xb8\xad"), UTF8_DONE);	/* U+4E2D */
	CHECK_EQ(ud.size, 3);
	CHECK_EQ(ud.width, 2);

	CHECK_EQ(decode(&ud, "\xf0\x9f\x98\x80"), UTF8_DONE);	/* U+1F600 */
	CHECK_EQ(ud.size, 4);
	CHECK_EQ(ud.width, 2);

	CHECK_EQ(decode(&ud, "\xcc\x81"), UTF8_DONE);		/* U+0301 */
	CHECK_EQ(ud.width, 0);
}

TEST(utf8, append_rejects_bad_continuation)
{
	struct utf8_data	ud;

	CHECK_EQ(utf8_open(&ud, 0xc3), UTF8_MORE);
	CHECK_EQ(utf8_append(&ud, 'a'), UTF8_ERROR);
	CHECK_EQ(ud.width, 0xff);

	CHECK_EQ(utf8_open(&ud, 0xe4), UTF8_MORE);
	CHECK_EQ(utf8_append(&ud, 0xb8), UTF8_MORE);
	CHECK_EQ(utf8_append(&ud, 0x41), UTF8_ERROR);
}

TEST(utf8, append_rejects_overlong_and_surrogates)
{
	struct utf8_data	ud;

	CHECK(decode(&ud, "\xe0\x80\x80") != UTF8_DONE);	/* overlong NUL */
	CHECK(decode(&ud, "\xf0\x80\x80\x80") != UTF8_DONE);	/* overlong */
	CHECK(decode(&ud, "\xed\xa0\x80") != UTF8_DONE);	/* U+D800 */
	CHECK(decode(&ud, "\xf4\x90\x80\x80") != UTF8_DONE);	/* > U+10FFFF */
}

TEST(utf8, truncated_sequence_stays_incomplete)
{
	struct utf8_data	ud;

	CHECK_EQ(decode(&ud, "\xe4\xb8"), UTF8_MORE);
	CHECK_EQ(ud.have, 2);
	CHECK_EQ(ud.size, 3);
}

TEST(utf8, set_and_copy)
{
	struct utf8_data	a, b;

	utf8_set(&a, 'x');
	CHECK_EQ(a.size, 1);
	CHECK_EQ(a.have, 1);
	CHECK_EQ(a.width, 1);
	CHECK_EQ(a.data[0], 'x');
	utf8_copy(&b, &a);
	CHECK(memcmp(&a, &b, sizeof a) == 0);
}

TEST(utf8, towc_and_fromwc_roundtrip)
{
	struct utf8_data	ud;
	wchar_t			wc = 0;

	CHECK_EQ(utf8_fromwc(0x4e2d, &ud), UTF8_DONE);
	CHECK_EQ(ud.size, 3);
	CHECK(memcmp(ud.data, "\xe4\xb8\xad", 3) == 0);
	CHECK_EQ(ud.width, 2);
	CHECK_EQ(utf8_towc(&ud, &wc), UTF8_DONE);
	CHECK_EQ((long long)wc, 0x4e2d);

	CHECK_EQ(utf8_fromwc(0x1f600, &ud), UTF8_DONE);
	CHECK_EQ(ud.size, 4);
	CHECK_EQ(utf8_towc(&ud, &wc), UTF8_DONE);
	CHECK_EQ((long long)wc, 0x1f600);
}

TEST(utf8, packed_char_roundtrips_every_size)
{
	static const char	*cases[] = { "a", "\xc3\xa9", "\xe4\xb8\xad",
	    "\xf0\x9f\x98\x80", nullptr };
	struct utf8_data	 in, out;
	utf8_char		 uc;
	u_int			 i;

	for (i = 0; cases[i] != nullptr; i++) {
		REQUIRE_EQ(decode(&in, cases[i]), UTF8_DONE);
		CHECK_EQ(utf8_from_data(&in, &uc), UTF8_DONE);
		utf8_to_data(uc, &out);
		CHECK_EQ(out.size, in.size);
		CHECK_EQ(out.width, in.width);
		CHECK(memcmp(out.data, in.data, in.size) == 0);
	}
	uc = utf8_build_one('z');
	utf8_to_data(uc, &out);
	CHECK_EQ(out.size, 1);
	CHECK_EQ(out.width, 1);
	CHECK_EQ(out.data[0], 'z');
}

TEST(utf8, isvalid_accepts_printable_and_utf8_only)
{
	CHECK_EQ(utf8_isvalid("hello"), 1);
	CHECK_EQ(utf8_isvalid("caf\xc3\xa9 \xe4\xb8\xad"), 1);
	CHECK_EQ(utf8_isvalid(""), 1);
	CHECK_EQ(utf8_isvalid("a\tb"), 0);
	CHECK_EQ(utf8_isvalid("a\x7f"), 0);
	CHECK_EQ(utf8_isvalid("\xc3"), 0);
	CHECK_EQ(utf8_isvalid("\xc3(x"), 0);
	CHECK_EQ(utf8_isvalid("\xff"), 0);
}

TEST(utf8, sanitize_replaces_by_width)
{
	char	*s;

	s = utf8_sanitize("h\xc3\xa9llo");
	CHECK_EQ(s, "h_llo");
	free(s);
	s = utf8_sanitize("\xe4\xb8\xad" "x");
	CHECK_EQ(s, "__x");
	free(s);
	s = utf8_sanitize("a\x01\x7f" "b\xff" "c");
	CHECK_EQ(s, "a__b_c");
	free(s);
	s = utf8_sanitize("");
	CHECK_EQ(s, "");
	free(s);
}

TEST(utf8, strvis_passes_utf8_and_escapes_controls)
{
	char	buf[64], *out = nullptr;
	size_t	n;

	n = utf8_strvis(buf, "\xc3\xa9\033x", 4, VIS_OCTAL);
	CHECK_EQ(buf, "\xc3\xa9\\033x");
	CHECK_EQ(n, strlen(buf));

	n = utf8_stravis(&out, "\xe4\xb8\xad\t", VIS_OCTAL | VIS_TAB);
	REQUIRE_NONNULL(out);
	CHECK_EQ(out, "\xe4\xb8\xad\\011");
	CHECK_EQ(n, strlen(out));
	free(out);

	n = utf8_strvis(buf, "\xc3" "a", 2, VIS_OCTAL);
	CHECK_EQ(buf, "\\303a");
}

TEST(utf8, cstr_conversions_and_widths)
{
	struct utf8_data	*ud;
	char			*s;

	ud = utf8_fromcstr("a\xc3\xa9\xe4\xb8\xad");
	REQUIRE_NONNULL(ud);
	CHECK_EQ(utf8_strlen(ud), 3u);
	CHECK_EQ(utf8_strwidth(ud, -1), 4u);
	CHECK_EQ(utf8_strwidth(ud, 2), 2u);
	s = utf8_tocstr(ud);
	CHECK_EQ(s, "a\xc3\xa9\xe4\xb8\xad");
	free(s);
	free(ud);

	CHECK_EQ(utf8_cstrwidth("a\xc3\xa9\xe4\xb8\xad"), 4u);
	CHECK_EQ(utf8_cstrwidth(""), 0u);
	CHECK_EQ(utf8_cstrwidth("\xc3" "x"), 1u);
}

TEST(utf8, pad_by_display_width)
{
	char	*s;

	s = utf8_padcstr("\xe4\xb8\xad", 5);
	CHECK_EQ(s, "\xe4\xb8\xad   ");
	free(s);
	s = utf8_rpadcstr("ab", 4);
	CHECK_EQ(s, "  ab");
	free(s);
	s = utf8_padcstr("toolong", 3);
	CHECK_EQ(s, "toolong");
	free(s);
}

TEST(utf8, cstrhas_finds_character)
{
	struct utf8_data	ud;

	REQUIRE_EQ(decode(&ud, "\xe4\xb8\xad"), UTF8_DONE);
	CHECK_EQ(utf8_cstrhas("a\xe4\xb8\xad" "b", &ud), 1);
	CHECK_EQ(utf8_cstrhas("ab", &ud), 0);
	utf8_set(&ud, 'b');
	CHECK_EQ(utf8_cstrhas("ab", &ud), 1);
}

TEST(utf8, combined_predicates)
{
	struct utf8_data	zwj, vs, filler, a, b;

	REQUIRE_EQ(decode(&zwj, "\xe2\x80\x8d"), UTF8_DONE);	/* U+200D */
	REQUIRE_EQ(decode(&vs, "\xef\xb8\x8f"), UTF8_DONE);	/* U+FE0F */
	REQUIRE_EQ(decode(&filler, "\xe3\x85\xa4"), UTF8_DONE);	/* U+3164 */
	utf8_set(&a, 'a');
	utf8_set(&b, 'b');

	CHECK_EQ(utf8_is_zwj(&zwj), 1);
	CHECK_EQ(utf8_has_zwj(&zwj), 1);
	CHECK_EQ(utf8_is_zwj(&vs), 0);
	CHECK_EQ(utf8_is_vs(&vs), 1);
	CHECK_EQ(utf8_is_vs(&zwj), 0);
	CHECK_EQ(utf8_is_hangul_filler(&filler), 1);
	CHECK_EQ(utf8_is_hangul_filler(&a), 0);
	CHECK_EQ(utf8_is_zwj(&a), 0);
	CHECK_EQ(utf8_has_zwj(&a), 0);
	CHECK_EQ(hanguljamo_check_state(&a, &b), HANGULJAMO_STATE_NOT_HANGULJAMO);
}

TEST(utf8, regional_indicators_combine_in_pairs)
{
	struct utf8_data	ri1, ri2, a;

	REQUIRE_EQ(decode(&ri1, "\xf0\x9f\x87\xa6"), UTF8_DONE);	/* U+1F1E6 */
	REQUIRE_EQ(decode(&ri2, "\xf0\x9f\x87\xa8"), UTF8_DONE);	/* U+1F1E8 */
	utf8_set(&a, 'a');
	CHECK_EQ(utf8_should_combine(&ri1, &ri2), 1);
	CHECK_EQ(utf8_should_combine(&a, &ri2), 0);
	CHECK_EQ(utf8_should_combine(&ri1, &a), 0);
}

TEST(utf8, stravisx_bounds_length_and_vis_dq_escapes_dollar)
{
	char	 buf[4 * 8 + 1], *out = nullptr;
	size_t	 n;

	n = utf8_stravisx(&out, "ab\033cd", 3, VIS_OCTAL);
	REQUIRE_NONNULL(out);
	CHECK_EQ(out, "ab\\033");
	CHECK_EQ(n, strlen(out));
	free(out);

	n = utf8_strvis(buf, "$x $1 ${", 8, VIS_DQ);
	CHECK_EQ(buf, "\\$x $1 \\${");
	CHECK_EQ(n, strlen(buf));
}
