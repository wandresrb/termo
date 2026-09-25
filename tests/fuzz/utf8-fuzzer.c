#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"

enum utf8_state termo_c_utf8_towc(const struct utf8_data *, wchar_t *);
enum utf8_state termo_c_utf8_fromwc(wchar_t, struct utf8_data *);
void termo_c_utf8_update_width_cache(void);
enum utf8_state termo_c_utf8_from_data(const struct utf8_data *, utf8_char *);
void termo_c_utf8_to_data(utf8_char, struct utf8_data *);
enum utf8_state termo_c_utf8_open(struct utf8_data *, u_char);
enum utf8_state termo_c_utf8_append(struct utf8_data *, u_char);
int termo_c_utf8_isvalid(const char *);
size_t termo_c_utf8_strvis(char *, const char *, size_t, int);
size_t termo_c_utf8_stravisx(char **, const char *, size_t, int);
char *termo_c_utf8_sanitize(const char *);
size_t termo_c_utf8_strlen(const struct utf8_data *);
u_int termo_c_utf8_strwidth(const struct utf8_data *, ssize_t);
struct utf8_data *termo_c_utf8_fromcstr(const char *);
char *termo_c_utf8_tocstr(struct utf8_data *);
u_int termo_c_utf8_cstrwidth(const char *);
char *termo_c_utf8_padcstr(const char *, u_int);
char *termo_c_utf8_rpadcstr(const char *, u_int);
int termo_c_utf8_cstrhas(const char *, const struct utf8_data *);
int termo_c_utf8_has_zwj(const struct utf8_data *);
int termo_c_utf8_is_zwj(const struct utf8_data *);
int termo_c_utf8_is_vs(const struct utf8_data *);
int termo_c_utf8_is_hangul_filler(const struct utf8_data *);
int termo_c_utf8_should_combine(const struct utf8_data *, const struct utf8_data *);
enum hanguljamo_state termo_c_hanguljamo_check_state(const struct utf8_data *,
    const struct utf8_data *);

int LLVMFuzzerTestOneInput(const u_char *, size_t);
int LLVMFuzzerInitialize(int *, char ***);

static constexpr size_t INPUT_MAX = 512;
static constexpr utf8_char PACKED_NO_WIDTH = 0x1fffffff;

static const int flags[] = {
	VIS_OCTAL,
	VIS_OCTAL | VIS_TAB | VIS_NL,
	VIS_CSTYLE | VIS_WHITE,
	VIS_OCTAL | VIS_DQ,
	VIS_CSTYLE | VIS_SAFE | VIS_GLOB,
	VIS_OCTAL | VIS_NOSLASH,
	VIS_OCTAL | VIS_ALL,
	VIS_CSTYLE | VIS_DQ | VIS_SP,
};

[[noreturn]] static void
differ(const char *what)
{
	fprintf(stderr, "utf8 divergence: %s\n", what);
	abort();
}

static void
same_bytes(const struct utf8_data *c, const struct utf8_data *r, const char *what)
{
	if (c->size != r->size || c->have != r->have ||
	    memcmp(c->data, r->data, sizeof c->data) != 0)
		differ(what);
}

/*
 * With VIS_DQ the C module asks isalpha() whether a '$' needs a backslash;
 * for a stray byte above 0x7f the answer depends on the host's ctype tables
 * (yes on macOS, no on glibc). Rust escapes before ASCII names only.
 */
static bool
dollar_before_high_byte(const u_char *data, size_t size, int flag)
{
	size_t	i;

	if ((flag & VIS_DQ) == 0)
		return false;
	for (i = 0; i + 1 < size; i++) {
		if (data[i] == '$' && data[i + 1] >= 0x80)
			return true;
	}
	return false;
}

/*
 * Every function whose result depends on a character's width is compared
 * only when both implementations agreed on every width in the input
 * (docs/width-delta.md records where wcwidth and unicode-width differ).
 */
static bool
decoder(const u_char *data, size_t size)
{
	struct utf8_data	c, r, prev_c, prev_r, wc_c, wc_r;
	enum utf8_state		cs, rs;
	utf8_char		uc, ur;
	wchar_t			cw, rw;
	size_t			i, start = 0;
	bool			widths_agree = true, have_prev = false;

	memset(&c, 0, sizeof c);
	memset(&r, 0, sizeof r);
	cs = rs = UTF8_ERROR;
	for (i = 0; i < size; i++) {
		if (cs == UTF8_MORE) {
			cs = termo_c_utf8_append(&c, data[i]);
			rs = utf8_append(&r, data[i]);
		} else {
			start = i;
			cs = termo_c_utf8_open(&c, data[i]);
			rs = utf8_open(&r, data[i]);
		}
		if (cs != rs)
			differ("decoder state");
		same_bytes(&c, &r, "decoder bytes");
		if ((c.width == 0xff) != (r.width == 0xff))
			differ("decoder bad-continuation mark");
		if (cs == UTF8_ERROR)
			i = start;
		if (cs != UTF8_DONE)
			continue;
		if (c.width != r.width)
			widths_agree = false;

		if (termo_c_utf8_towc(&c, &cw) != utf8_towc(&r, &rw) || cw != rw)
			differ("towc");
		if (termo_c_utf8_fromwc(cw, &wc_c) != utf8_fromwc(rw, &wc_r))
			differ("fromwc state");
		if (wc_c.size != wc_r.size ||
		    memcmp(wc_c.data, wc_r.data, wc_c.size) != 0)
			differ("fromwc bytes");

		if (termo_c_utf8_from_data(&c, &uc) != utf8_from_data(&r, &ur) ||
		    (uc & PACKED_NO_WIDTH) != (ur & PACKED_NO_WIDTH))
			differ("from_data");
		termo_c_utf8_to_data(uc, &wc_c);
		utf8_to_data(ur, &wc_r);
		same_bytes(&wc_c, &wc_r, "to_data");

		if (termo_c_utf8_has_zwj(&c) != utf8_has_zwj(&r) ||
		    termo_c_utf8_is_zwj(&c) != utf8_is_zwj(&r) ||
		    termo_c_utf8_is_vs(&c) != utf8_is_vs(&r) ||
		    termo_c_utf8_is_hangul_filler(&c) != utf8_is_hangul_filler(&r))
			differ("combined predicate");
		if (have_prev) {
			if (termo_c_utf8_should_combine(&prev_c, &c) !=
			    utf8_should_combine(&prev_r, &r))
				differ("should_combine");
			if (termo_c_hanguljamo_check_state(&prev_c, &c) !=
			    hanguljamo_check_state(&prev_r, &r))
				differ("hanguljamo_check_state");
		}
		prev_c = c;
		prev_r = r;
		have_prev = true;
	}
	return widths_agree;
}

static void
strings(const u_char *data, size_t size, bool widths_agree)
{
	char			 s[INPUT_MAX + 1], bc[4 * INPUT_MAX + 1], br[4 * INPUT_MAX + 1];
	char			*oc, *orr;
	struct utf8_data	*uc, *ur, *last;
	size_t			 n, lc, lr, i;
	int			 flag;
	u_int			 width;

	flag = flags[data[0] % nitems(flags)];
	width = data[0];
	data++;
	size--;

	if (!dollar_before_high_byte(data, size, flag)) {
		lc = termo_c_utf8_strvis(bc, (const char *)data, size, flag);
		lr = utf8_strvis(br, (const char *)data, size, flag);
		if (lc != lr || memcmp(bc, br, lc + 1) != 0)
			differ("strvis");
		lc = termo_c_utf8_stravisx(&oc, (const char *)data, size, flag);
		lr = utf8_stravisx(&orr, (const char *)data, size, flag);
		if (lc != lr || memcmp(oc, orr, lc + 1) != 0)
			differ("stravisx");
		free(oc);
		free(orr);
	}

	memcpy(s, data, size);
	s[size] = '\0';
	n = strlen(s);

	if (termo_c_utf8_isvalid(s) != utf8_isvalid(s))
		differ("isvalid");

	uc = termo_c_utf8_fromcstr(s);
	ur = utf8_fromcstr(s);
	lc = termo_c_utf8_strlen(uc);
	lr = utf8_strlen(ur);
	if (lc != lr)
		differ("fromcstr length");
	if (uc[lc].size != 0 || ur[lc].size != 0)
		differ("fromcstr terminator");
	for (i = 0; i < lc; i++) {
		same_bytes(&uc[i], &ur[i], "fromcstr item");
		if (widths_agree && uc[i].width != ur[i].width)
			differ("fromcstr width");
	}
	oc = termo_c_utf8_tocstr(uc);
	orr = utf8_tocstr(ur);
	if (strcmp(oc, orr) != 0 || (size_t)strlen(oc) > n)
		differ("tocstr");
	free(oc);
	free(orr);
	last = lc > 0 ? &ur[lc - 1] : &ur[0];
	if (termo_c_utf8_cstrhas(s, last) != utf8_cstrhas(s, last))
		differ("cstrhas");

	if (widths_agree) {
		if (termo_c_utf8_strwidth(uc, -1) != utf8_strwidth(ur, -1) ||
		    termo_c_utf8_strwidth(uc, width % 8) != utf8_strwidth(ur, width % 8))
			differ("strwidth");
		if (termo_c_utf8_cstrwidth(s) != utf8_cstrwidth(s))
			differ("cstrwidth");
		/* The C module calls xreallocarray(NULL, 0, 1) and aborts. */
		if (lc == 0 || uc[0].width != 0) {
			oc = termo_c_utf8_sanitize(s);
			orr = utf8_sanitize(s);
			if (strcmp(oc, orr) != 0)
				differ("sanitize");
			free(oc);
			free(orr);
		}
		oc = termo_c_utf8_padcstr(s, width);
		orr = utf8_padcstr(s, width);
		if (strcmp(oc, orr) != 0)
			differ("padcstr");
		free(oc);
		free(orr);
		oc = termo_c_utf8_rpadcstr(s, width);
		orr = utf8_rpadcstr(s, width);
		if (strcmp(oc, orr) != 0)
			differ("rpadcstr");
		free(oc);
		free(orr);
	}
	free(uc);
	free(ur);
}

int
LLVMFuzzerTestOneInput(const u_char *data, size_t size)
{
	bool	widths_agree;

	if (size < 2 || size > INPUT_MAX)
		return 0;
	widths_agree = decoder(data + 1, size - 1);
	strings(data, size, widths_agree);
	return 0;
}

int
LLVMFuzzerInitialize([[maybe_unused]] int *argc, [[maybe_unused]] char ***argv)
{
	termo_test_init();
	termo_c_utf8_update_width_cache();
	return 0;
}
