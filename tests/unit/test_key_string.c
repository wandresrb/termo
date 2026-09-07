#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "test.h"

TEST(key_string, ascii_and_modifiers)
{
	CHECK_EQ(key_string_lookup_string("a"), (key_code)'a');
	CHECK_EQ(key_string_lookup_string("C-a"), 'a' | KEYC_CTRL);
	CHECK_EQ(key_string_lookup_string("^a"), 'a' | KEYC_CTRL);
	CHECK_EQ(key_string_lookup_string("^A"), 'a' | KEYC_CTRL);
	CHECK_EQ(key_string_lookup_string("M-x"), 'x' | KEYC_META);
	CHECK_EQ(key_string_lookup_string("S-a"), 'a' | KEYC_SHIFT);
	CHECK_EQ(key_string_lookup_string("C-M-x"), 'x' | KEYC_CTRL | KEYC_META);
	CHECK_EQ(key_string_lookup_string("M-C-x"), 'x' | KEYC_CTRL | KEYC_META);
}

TEST(key_string, named_keys)
{
	CHECK_EQ(key_string_lookup_string("Escape"), (key_code)'\033');
	CHECK_EQ(key_string_lookup_string("Enter"), (key_code)'\r');
	CHECK_EQ(key_string_lookup_string("Tab"), (key_code)'\t');
	CHECK_EQ(key_string_lookup_string("Space"), (key_code)' ');
	CHECK_EQ(key_string_lookup_string("F1"), KEYC_F1);
	CHECK_EQ(key_string_lookup_string("f12"), KEYC_F12);
	CHECK_EQ(key_string_lookup_string("Home"), KEYC_HOME);
	CHECK_EQ(key_string_lookup_string("PageUp"), KEYC_PPAGE);
	CHECK_EQ(key_string_lookup_string("PPage"), KEYC_PPAGE);
	CHECK_EQ(key_string_lookup_string("BSpace"), KEYC_BSPACE);
	CHECK_EQ(key_string_lookup_string("Up"), KEYC_UP | KEYC_CURSOR);
	CHECK_EQ(key_string_lookup_string("KP*"), KEYC_KP_STAR | KEYC_KEYPAD);
	CHECK_EQ(key_string_lookup_string("S-F1"), KEYC_F1 | KEYC_SHIFT);
	CHECK_EQ(key_string_lookup_string("M-F1"),
	    KEYC_F1 | KEYC_META | KEYC_IMPLIED_META);
	CHECK_EQ(key_string_lookup_string("MouseDown1Pane"), KEYC_MOUSEDOWN1_PANE);
}

TEST(key_string, special_forms)
{
	struct utf8_data	ud;
	key_code		key;

	CHECK_EQ(key_string_lookup_string("None"), KEYC_NONE);
	CHECK_EQ(key_string_lookup_string("any"), KEYC_ANY);
	CHECK_EQ(key_string_lookup_string("0x1b"), (key_code)0x1b);
	CHECK_EQ(key_string_lookup_string("[BEL]"), (key_code)7);

	/* Upstream quirk: hex above 0x1f is packed as a UTF-8 key even for ASCII. */
	key = key_string_lookup_string("0x41");
	CHECK(key != (key_code)'A');
	utf8_to_data(key & KEYC_MASK_KEY, &ud);
	CHECK_EQ(ud.size, 1);
	CHECK_EQ(ud.data[0], 'A');
}

TEST(key_string, utf8_keys)
{
	key_code		 key = key_string_lookup_string("\xc3\xa9");
	struct utf8_data	 ud;

	CHECK(KEYC_IS_UNICODE(key));
	utf8_to_data(key & KEYC_MASK_KEY, &ud);
	CHECK_EQ(ud.size, 2);
	CHECK(memcmp(ud.data, "\xc3\xa9", 2) == 0);
	CHECK_EQ(key_string_lookup_string("M-\xc3\xa9"), key | KEYC_META);
	CHECK_EQ(key_string_lookup_key(key, 0), "\xc3\xa9");
}

TEST(key_string, rejects_garbage)
{
	CHECK_EQ(key_string_lookup_string("Nope"), KEYC_UNKNOWN);
	CHECK_EQ(key_string_lookup_string(""), KEYC_UNKNOWN);
	CHECK_EQ(key_string_lookup_string("C-"), KEYC_UNKNOWN);
	CHECK_EQ(key_string_lookup_string("\x01"), KEYC_UNKNOWN);
	CHECK_EQ(key_string_lookup_string("0xzz"), KEYC_UNKNOWN);
	CHECK_EQ(key_string_lookup_string("\xc3"), KEYC_UNKNOWN);
	CHECK_EQ(key_string_lookup_string("\xc3\xa9x"), KEYC_UNKNOWN);
}

TEST(key_string, lookup_key_roundtrips_canonical_names)
{
	static const char	*names[] = { "a", "Z", "C-a", "M-x", "S-a",
	    "C-M-x", "F1", "F12", "S-F1", "C-F5", "Home", "End", "NPage",
	    "PPage", "IC", "DC", "BTab", "Space", "BSpace", "Tab", "Enter",
	    "Escape", "Up", "Down", "Left", "Right", "KP*", "KP/", "KP0",
	    "MouseDown1Pane", "MouseUp3Status", "WheelUpPane", nullptr };
	u_int			 i;
	key_code		 key;

	for (i = 0; names[i] != nullptr; i++) {
		key = key_string_lookup_string(names[i]);
		if (key == KEYC_UNKNOWN) {
			test_fail(__FILE__, __LINE__, "%s: unknown", names[i]);
			continue;
		}
		CHECK_EQ(key_string_lookup_key(key, 0), names[i]);
	}
}

TEST(key_string, lookup_key_prints_control_and_flags)
{
	CHECK_EQ(key_string_lookup_key('a' | KEYC_CTRL, 0), "C-a");
	CHECK_EQ(key_string_lookup_key(0x01, 0), "[SOH]");
	CHECK_EQ(key_string_lookup_key(0x1b, 0), "Escape");
	CHECK_EQ(key_string_lookup_key(0x7f, 0), "C-?");
	CHECK_EQ(key_string_lookup_key(KEYC_BSPACE, 0), "BSpace");
	CHECK_EQ(key_string_lookup_key(KEYC_NONE, 0), "None");
	CHECK_EQ(key_string_lookup_key(KEYC_UNKNOWN, 0), "Unknown");
	CHECK_NONNULL(strstr(key_string_lookup_key('a' | KEYC_LITERAL, 1), "["));
}
