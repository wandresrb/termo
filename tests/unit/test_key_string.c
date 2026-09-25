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

static void
roundtrip(const char *name)
{
	char		 with[64];
	key_code	 key;
	const char	*s;

	key = key_string_lookup_string(name);
	if (key == KEYC_UNKNOWN) {
		test_fail(__FILE__, __LINE__, "%s: unknown", name);
		return;
	}
	s = key_string_lookup_key(key, 0);
	if (strcmp(s, name) != 0)
		test_fail(__FILE__, __LINE__, "%s: prints as %s", name, s);
	if (name[0] == '[')
		return;
	xsnprintf(with, sizeof with, "C-M-S-%s", name);
	s = key_string_lookup_key(key_string_lookup_string(with), 0);
	if (strcmp(s, with) != 0)
		test_fail(__FILE__, __LINE__, "%s: prints as %s", with, s);
}

TEST(key_string, whole_table_roundtrips_both_ways)
{
	static const char	*names[] = { "F1", "F2", "F3", "F4", "F5", "F6",
	    "F7", "F8", "F9", "F10", "F11", "F12", "IC", "DC", "Home", "End",
	    "NPage", "PPage", "BTab", "Space", "BSpace", "[NUL]", "[SOH]",
	    "[STX]", "[ETX]", "[EOT]", "[ENQ]", "[ASC]", "[BEL]", "[BS]", "Tab",
	    "[LF]", "[VT]", "[FF]", "Enter", "[SO]", "[SI]", "[DLE]", "[DC1]",
	    "[DC2]", "[DC3]", "[DC4]", "[NAK]", "[SYN]", "[ETB]", "[CAN]", "[EM]",
	    "[SUB]", "Escape", "[FS]", "[GS]", "[RS]", "[US]", "Up", "Down",
	    "Left", "Right", "KP/", "KP*", "KP-", "KP7", "KP8", "KP9", "KP+",
	    "KP4", "KP5", "KP6", "KP1", "KP2", "KP3", "KPEnter", "KP0", "KP.",
	    nullptr };
	static const char	*prefixes[] = { "MouseDown1", "MouseDown2",
	    "MouseDown3", "MouseDown6", "MouseDown7", "MouseDown8", "MouseDown9",
	    "MouseDown10", "MouseDown11", "MouseUp1", "MouseUp2", "MouseUp3",
	    "MouseUp6", "MouseUp7", "MouseUp8", "MouseUp9", "MouseUp10",
	    "MouseUp11", "MouseDrag1", "MouseDrag2", "MouseDrag3", "MouseDrag6",
	    "MouseDrag7", "MouseDrag8", "MouseDrag9", "MouseDrag10", "MouseDrag11",
	    "MouseDragEnd1", "MouseDragEnd2", "MouseDragEnd3", "MouseDragEnd6",
	    "MouseDragEnd7", "MouseDragEnd8", "MouseDragEnd9", "MouseDragEnd10",
	    "MouseDragEnd11", "WheelUp", "WheelDown", "SecondClick1",
	    "SecondClick2", "SecondClick3", "SecondClick6", "SecondClick7",
	    "SecondClick8", "SecondClick9", "SecondClick10", "SecondClick11",
	    "DoubleClick1", "DoubleClick2", "DoubleClick3", "DoubleClick6",
	    "DoubleClick7", "DoubleClick8", "DoubleClick9", "DoubleClick10",
	    "DoubleClick11", "TripleClick1", "TripleClick2", "TripleClick3",
	    "TripleClick6", "TripleClick7", "TripleClick8", "TripleClick9",
	    "TripleClick10", "TripleClick11", nullptr };
	static const char	*suffixes[] = { "Pane", "Status", "StatusLeft",
	    "StatusRight", "StatusDefault", "ScrollbarUp", "ScrollbarSlider",
	    "ScrollbarDown", "Empty", "Border", "Control0", "Control1",
	    "Control2", "Control3", "Control4", "Control5", "Control6",
	    "Control7", "Control8", "Control9", nullptr };
	static const struct {
		const char	*alias;
		const char	*canonical;
	} aliases[] = { { "Insert", "IC" }, { "Delete", "DC" },
	    { "PageDown", "NPage" }, { "PgDn", "NPage" }, { "PageUp", "PPage" },
	    { "PgUp", "PPage" } };
	char			 name[64];
	u_int			 i, j;

	for (i = 0; names[i] != nullptr; i++)
		roundtrip(names[i]);
	for (i = 0; prefixes[i] != nullptr; i++) {
		for (j = 0; suffixes[j] != nullptr; j++) {
			xsnprintf(name, sizeof name, "%s%s", prefixes[i],
			    suffixes[j]);
			roundtrip(name);
		}
	}
	for (i = 0; i < nitems(aliases); i++) {
		if (key_string_lookup_string(aliases[i].alias) !=
		    key_string_lookup_string(aliases[i].canonical)) {
			test_fail(__FILE__, __LINE__, "%s: not %s",
			    aliases[i].alias, aliases[i].canonical);
		}
	}
}
