/*
 * Every compat shim through the name the tree uses. If meson picked the
 * wrong one between libc and src/compat, it fails here instead of in the
 * server.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <resolv.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "termo.h"
#include "test.h"

/* The flags log.c, term.c, capture.c and util.c pass. */
constexpr int	tree_vis_flags = VIS_OCTAL|VIS_CSTYLE|VIS_TAB|VIS_NL;

TEST(compat, strnvis_escapes_into_dst)
{
	char	out[64];

	memset(out, 'X', sizeof out);
	strnvis(out, "\033[1m", sizeof out, VIS_OCTAL);
	CHECK_EQ(out, "\\033[1m");

	strnvis(out, "a\tb", sizeof out, VIS_OCTAL | VIS_TAB);
	CHECK_EQ(out, "a\\011b");
}

TEST(compat, strnvis_truncates_to_dst_length)
{
	char	out[4];

	strnvis(out, "abcdef", sizeof out, VIS_OCTAL);
	CHECK_EQ(out, "abc");
}

TEST(compat, stravis_allocates)
{
	char	*out = nullptr;

	CHECK(stravis(&out, "\001x", VIS_OCTAL) != -1);
	REQUIRE_NONNULL(out);
	CHECK_EQ(out, "\\001x");
	free(out);
}

TEST(compat, vis_encodes_single_bytes_with_tree_flags)
{
	char	b[8];

	vis(b, '\033', tree_vis_flags, 0);
	CHECK_EQ(b, "\\033");
	vis(b, '\n', tree_vis_flags, 0);
	CHECK_EQ(b, "\\n");
	vis(b, '\t', tree_vis_flags, 0);
	CHECK_EQ(b, "\\t");
	vis(b, ' ', tree_vis_flags, 0);
	CHECK_EQ(b, " ");
	vis(b, '\\', tree_vis_flags, 0);
	CHECK_EQ(b, "\\\\");
	vis(b, '\\', VIS_NOSLASH, 0);
	CHECK_EQ(b, "\\");
	vis(b, '"', VIS_DQ, 0);
	CHECK_EQ(b, "\\\"");
	vis(b, 0x81, 0, 0);
	CHECK_EQ(b, "\\M^A");
	vis(b, 0xe9, 0, 0);
	CHECK_EQ(b, "\\M-i");
	vis(b, '\0', VIS_CSTYLE, '1');
	CHECK_EQ(b, "\\000");
	vis(b, '\0', VIS_CSTYLE, 'x');
	CHECK_EQ(b, "\\0");
	vis(b, '\a', VIS_SAFE, 0);
	CHECK_EQ(b, "\a");
	vis(b, 1, VIS_SAFE, 0);
	CHECK_EQ(b, "\\^A");
	vis(b, '*', VIS_GLOB, 0);
	CHECK_EQ(b, "\\052");
}

TEST(compat, strvis_strvisx_strnvis_agree)
{
	char	out[32], out4[4];

	CHECK_EQ(strvis(out, "a\tb\n", tree_vis_flags), 6);
	CHECK_EQ(out, "a\\tb\\n");
	CHECK_EQ(strvisx(out, "a\0b", 3, VIS_OCTAL), 6);
	CHECK_EQ(out, "a\\000b");
	CHECK_EQ(strvisx(out, "\0" "1", 2, VIS_CSTYLE), 5);
	CHECK_EQ(out, "\\0001");
	CHECK_EQ(strnvis(out4, "\033\033", sizeof out4, VIS_OCTAL), 8);
	CHECK_EQ(out4, "");
}

TEST(compat, strunvis_decodes_term_override_syntax)
{
	char	dst[16], c = 0;
	int	state = 0;

	CHECK_EQ(strunvis(dst, "\\033[1m"), 4);
	CHECK_EQ(dst, "\033[1m");
	CHECK_EQ(strunvis(dst, "\\E[1m"), 4);
	CHECK_EQ(dst, "\033[1m");
	CHECK_EQ(strunvis(dst, "\\n\\t\\s\\\\"), 4);
	CHECK_EQ(dst, "\n\t \\");
	CHECK_EQ(strunvis(dst, "\\^A\\^?"), 2);
	CHECK_EQ(dst, "\001\177");
	CHECK_EQ(strunvis(dst, "\\M-a"), 1);
	CHECK_EQ(dst, "\341");
	CHECK_EQ(strunvis(dst, "\\M^A"), 1);
	CHECK_EQ(dst, "\201");
	CHECK_EQ(strunvis(dst, "\\101\\1a"), 3);
	CHECK_EQ(dst, "A\001a");
	CHECK_EQ(strunvis(dst, "\\7"), 1);
	CHECK_EQ(dst[0], 7);
	CHECK_EQ(strunvis(dst, "\\$x"), 1);
	CHECK_EQ(dst, "x");
	CHECK_EQ(strunvis(dst, "\\q"), -1);
	CHECK_EQ(strunvis(dst, "ab\\q"), -1);
	CHECK_EQ(dst, "ab");
	CHECK_EQ(strunvis(dst, "abc\\"), 3);
	CHECK_EQ(dst, "abc");

	CHECK_EQ(unvis(&c, '\\', &state, 0), 0);
	CHECK_EQ(unvis(&c, '1', &state, 0), 0);
	CHECK_EQ(unvis(&c, 'x', &state, 0), UNVIS_VALIDPUSH);
	CHECK_EQ(c, 1);
	CHECK_EQ(unvis(&c, 'x', &state, 0), UNVIS_VALID);
	CHECK_EQ(c, 'x');
}

TEST(compat, vis_unvis_round_trip_all_bytes)
{
	static const int	flags[] = {
		tree_vis_flags, VIS_CSTYLE, VIS_OCTAL
	};
	char			buf[256], enc[4 * 256 + 1], dec[257];
	u_int			i;

	for (i = 0; i < sizeof buf; i++)
		buf[i] = i;
	for (i = 0; i < nitems(flags); i++) {
		strvisx(enc, buf, sizeof buf, flags[i]);
		CHECK_EQ(strunvis(dec, enc), 256);
		CHECK(memcmp(dec, buf, sizeof buf) == 0);
	}
}

TEST(compat, strlcpy_truncates_and_reports_source_length)
{
	char	buf[4];

	CHECK_EQ(strlcpy(buf, "hello", sizeof buf), 5u);
	CHECK_EQ(buf, "hel");
	CHECK_EQ(strlcpy(buf, "hi", sizeof buf), 2u);
	CHECK_EQ(buf, "hi");
}

TEST(compat, strlcat_appends_within_bounds)
{
	char	buf[8] = "ab";

	CHECK_EQ(strlcat(buf, "cdefgh", sizeof buf), 8u);
	CHECK_EQ(buf, "abcdefg");
}

TEST(compat, strtonum_enforces_range)
{
	const char	*err;

	CHECK_EQ(strtonum("42", 0, 100, &err), 42);
	CHECK_NULL(err);
	CHECK_EQ(strtonum("101", 0, 100, &err), 0);
	CHECK_EQ(err, "too large");
	CHECK_EQ(strtonum("-1", 0, 100, &err), 0);
	CHECK_EQ(err, "too small");
	CHECK_EQ(strtonum("4x", 0, 100, &err), 0);
	CHECK_EQ(err, "invalid");
}

TEST(compat, reallocarray_rejects_overflow)
{
	void	*p;

	errno = 0;
	p = reallocarray(nullptr, SIZE_MAX / 2, 4);
	CHECK_NULL(p);
	CHECK_EQ(errno, ENOMEM);

	p = reallocarray(nullptr, 4, 8);
	CHECK_NONNULL(p);
	free(p);
}

TEST(compat, recallocarray_zeroes_new_space)
{
	int	*p;

	p = recallocarray(nullptr, 0, 2, sizeof *p);
	REQUIRE_NONNULL(p);
	p[0] = 7;
	p[1] = 9;
	p = recallocarray(p, 2, 4, sizeof *p);
	REQUIRE_NONNULL(p);
	CHECK_EQ(p[0], 7);
	CHECK_EQ(p[1], 9);
	CHECK_EQ(p[2], 0);
	CHECK_EQ(p[3], 0);
	free(p);
}

TEST(compat, htonll_is_big_endian)
{
	uint64_t	v = htonll(1);
	u_char		b[8];

	memcpy(b, &v, sizeof b);
	CHECK_EQ(b[7], 1);
	CHECK_EQ(b[0], 0);
	CHECK_EQ(ntohll(htonll(0x0102030405060708ULL)), 0x0102030405060708ULL);
}

TEST(compat, b64_roundtrips)
{
	const u_char	in[] = "termo\0\xff";
	char		enc[32];
	u_char		dec[16];

	CHECK(b64_ntop(in, sizeof in, enc, sizeof enc) > 0);
	CHECK_EQ(enc, "dGVybW8A/wA=");
	CHECK_EQ(b64_pton(enc, dec, sizeof dec), (int)sizeof in);
	CHECK(memcmp(dec, in, sizeof in) == 0);
	CHECK_EQ(b64_pton("not base64!", dec, sizeof dec), -1);
}

TEST(compat, getprogname_is_set)
{
	REQUIRE_NONNULL(getprogname());
	CHECK(strlen(getprogname()) > 0);
}

TEST(compat, explicit_bzero_clears)
{
	char	buf[16];

	memset(buf, 'A', sizeof buf);
	explicit_bzero(buf, sizeof buf);
	for (size_t i = 0; i < sizeof buf; i++)
		CHECK_EQ(buf[i], 0);
}

TEST(compat, memmem_finds_needle)
{
	const char	hay[] = "abc\0def";

	CHECK(memmem(hay, sizeof hay, "\0d", 2) == hay + 3);
	CHECK_NULL(memmem(hay, sizeof hay, "xyz", 3));
}

TEST(compat, strnlen_and_strndup_stop_at_limit)
{
	char	*s;

	CHECK_EQ(strnlen("hello", 3), 3u);
	CHECK_EQ(strnlen("hi", 10), 2u);
	s = strndup("hello", 2);
	CHECK_EQ(s, "he");
	free(s);
}

TEST(compat, getpeereid_returns_own_ids)
{
	int	fds[2];
	uid_t	uid = (uid_t)-1;
	gid_t	gid = (gid_t)-1;

	REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
	CHECK_EQ(getpeereid(fds[0], &uid, &gid), 0);
	CHECK_EQ(uid, getuid());
	CHECK_EQ(gid, getgid());
	close(fds[0]);
	close(fds[1]);
}

static void
getopt_reset(void)
{
	optreset = 1;
	optind = 1;
	opterr = 0;
}

TEST(compat, getopt_parses_main_c_option_string_bsd_style)
{
	const char	opts[] = "2c:CDdf:hlL:NqS:T:uUvV";
	char		*argv1[] = {
		"termo", "-2", "-Lsock", "-vv", "-f", "a.conf", "-c",
		"ls -l", "--", "new", "-v", nullptr
	};
	char		*argv2[] = { "termo", "new", "-v", nullptr };
	char		*argv3[] = { "termo", "-L", nullptr };
	char		*argv4[] = { "termo", "-Z", nullptr };
	char		*argv5[] = {
		"termo", "-S", "/tmp/s", "-T", "256", nullptr
	};

	getopt_reset();
	CHECK_EQ(getopt(11, argv1, opts), '2');
	CHECK_EQ(getopt(11, argv1, opts), 'L');
	CHECK_EQ(optarg, "sock");
	CHECK_EQ(getopt(11, argv1, opts), 'v');
	CHECK_EQ(getopt(11, argv1, opts), 'v');
	CHECK_EQ(getopt(11, argv1, opts), 'f');
	CHECK_EQ(optarg, "a.conf");
	CHECK_EQ(getopt(11, argv1, opts), 'c');
	CHECK_EQ(optarg, "ls -l");
	CHECK_EQ(getopt(11, argv1, opts), -1);
	CHECK_EQ(optind, 9);
	CHECK_EQ(argv1[optind], "new");

	getopt_reset();
	CHECK_EQ(getopt(3, argv2, opts), -1);
	CHECK_EQ(optind, 1);
	CHECK_EQ(argv2[1], "new");
	CHECK_EQ(argv2[2], "-v");

	getopt_reset();
	CHECK_EQ(getopt(2, argv3, opts), '?');
	CHECK_EQ(optopt, 'L');

	getopt_reset();
	CHECK_EQ(getopt(2, argv4, opts), '?');
	CHECK_EQ(optopt, 'Z');

	getopt_reset();
	CHECK_EQ(getopt(5, argv5, opts), 'S');
	CHECK_EQ(optarg, "/tmp/s");
	CHECK_EQ(getopt(5, argv5, opts), 'T');
	CHECK_EQ(optarg, "256");
	CHECK_EQ(getopt(5, argv5, opts), -1);
	CHECK_EQ(optind, 5);
}

TEST(compat, imsg_round_trips_over_socketpair)
{
	struct imsgbuf	a, b;
	struct imsg	m;
	char		buf[4];
	int		fds[2], fd;

	REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
	REQUIRE_EQ(imsgbuf_init(&a, fds[0]), 0);
	REQUIRE_EQ(imsgbuf_init(&b, fds[1]), 0);
	imsgbuf_allow_fdpass(&a);
	imsgbuf_allow_fdpass(&b);

	CHECK_EQ(imsg_compose(&a, 7, 0, -1, -1, "hi", 3), 1);
	CHECK_EQ(imsgbuf_queuelen(&a), 1u);
	CHECK_EQ(imsgbuf_flush(&a), 0);
	CHECK_EQ(imsgbuf_queuelen(&a), 0u);
	CHECK_EQ(imsgbuf_read(&b), 1);
	REQUIRE_EQ(imsgbuf_get(&b, &m), 1);
	CHECK_EQ(m.hdr.type, 7u);
	CHECK_EQ(imsg_get_type(&m), 7u);
	CHECK_EQ(imsg_get_len(&m), 3u);
	CHECK_EQ(imsg_get_data(&m, buf, 3), 0);
	CHECK_EQ(buf, "hi");
	CHECK_EQ(imsg_get_fd(&m), -1);
	imsg_free(&m);
	CHECK_EQ(imsgbuf_get(&b, &m), 0);

	fd = open("/dev/null", O_RDONLY);
	REQUIRE(fd >= 0);
	CHECK_EQ(imsg_compose(&a, 8, 0, -1, fd, "x", 1), 1);
	CHECK_EQ(imsgbuf_flush(&a), 0);
	CHECK_EQ(imsgbuf_read(&b), 1);
	REQUIRE_EQ(imsgbuf_get(&b, &m), 1);
	CHECK_EQ(m.hdr.type, 8u);
	CHECK_EQ(imsg_get_len(&m), 1u);
	fd = imsg_get_fd(&m);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_GETFD) != -1);
	CHECK_EQ(imsg_get_fd(&m), -1);
	close(fd);
	imsg_free(&m);

	imsgbuf_clear(&a);
	imsgbuf_clear(&b);
	close(fds[0]);
	close(fds[1]);
}

#ifdef HAVE_UTF8PROC
TEST(compat, utf8proc_shim_widths_and_conversions)
{
	wchar_t	wc = 0;
	char	buf[8];

	CHECK_EQ(utf8proc_wcwidth('A'), 1);
	CHECK_EQ(utf8proc_wcwidth(0x4e2d), 2);
	CHECK_EQ(utf8proc_wcwidth(0x0301), 0);
	CHECK_EQ(utf8proc_wcwidth(0xe0a0), 1);
	CHECK_EQ(utf8proc_mbtowc(&wc, "\xe4\xb8\xad", 3), 3);
	CHECK_EQ(wc, 0x4e2d);
	CHECK_EQ(utf8proc_mbtowc(&wc, "\xff", 1), -1);
	CHECK_EQ(utf8proc_wctomb(buf, 0x4e2d), 3);
	CHECK(memcmp(buf, "\xe4\xb8\xad", 3) == 0);
	CHECK_EQ(utf8proc_wctomb(buf, 0x110000), -1);
}
#endif

/* Zeroing is unobservable after free; ASAN is the assertion for the sizes. */
TEST(compat, freezero_frees_exact_sizes)
{
	char	*p = xmalloc(64), *q = xmalloc(1 << 20);

	memset(p, 'A', 64);
	CHECK_EQ(p[63], 'A');
	freezero(p, 64);
	freezero(nullptr, 64);
	freezero(nullptr, 0);
	memset(q, 'B', 1 << 20);
	CHECK_EQ(q[(1 << 20) - 1], 'B');
	freezero(q, 1 << 20);
}

TEST(compat, closefrom_closes_everything_above)
{
	int	a = open("/dev/null", O_RDONLY), b = open("/dev/null", O_RDONLY);
	int	c = open("/dev/null", O_RDONLY);

	REQUIRE(a >= 0 && b > a && c > b);
	closefrom(b);
	CHECK(fcntl(a, F_GETFD) != -1);
	CHECK_EQ(fcntl(b, F_GETFD), -1);
	CHECK_EQ(fcntl(c, F_GETFD), -1);
	close(a);
}
