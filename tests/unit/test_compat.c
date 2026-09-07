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

TEST(compat, freezero_accepts_null)
{
	char	*p = xmalloc(8);

	freezero(nullptr, 0);
	freezero(p, 8);
	CHECK(1);
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
