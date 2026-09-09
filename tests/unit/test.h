/*
 * Unit test support. A test is a function registered at load time:
 *
 *	TEST(format, expressions)
 *	{
 *		CHECK_EQ(expand("#{e|+|:2,3}"), "5");
 *	}
 *
 * CHECK* record a failure and continue; REQUIRE* record and return from the
 * test. Nothing catches signals: a crash dies with the sanitizer report.
 */

#ifndef TERMO_TEST_H
#define TERMO_TEST_H

#include <stddef.h>

struct test {
	const char	*module;
	const char	*name;
	void		(*fn)(void);
	struct test	*next;
};

void	test_register(struct test *);
[[gnu::format(printf, 3, 4)]] int	test_fail(const char *, int, const char *, ...);
int	test_eq_int(const char *, int, const char *, long long, long long);
int	test_eq_uint(const char *, int, const char *, unsigned long long,
	    unsigned long long);
int	test_eq_dbl(const char *, int, const char *, double, double);
int	test_eq_str(const char *, int, const char *, const char *,
	    const char *);
int	test_eq_ptr(const char *, int, const char *, const void *,
	    const void *);

static inline int
test_check(const char *file, int line, const char *expr, int ok)
{
	if (!ok)
		test_fail(file, line, "%s", expr);
	return (ok);
}

#define TEST(mod, nm)							\
	static void test_##mod##_##nm(void);				\
	static struct test test_##mod##_##nm##_entry = {		\
		#mod, #nm, test_##mod##_##nm, nullptr			\
	};								\
	[[gnu::constructor]] static void				\
	test_##mod##_##nm##_register(void)				\
	{								\
		test_register(&test_##mod##_##nm##_entry);		\
	}								\
	static void test_##mod##_##nm(void)

#define TEST_EQ(a, b) _Generic((a),					\
	char *: test_eq_str,						\
	const char *: test_eq_str,					\
	double: test_eq_dbl,						\
	float: test_eq_dbl,						\
	unsigned int: test_eq_uint,					\
	unsigned long: test_eq_uint,					\
	unsigned long long: test_eq_uint,				\
	default: test_eq_int						\
	)(__FILE__, __LINE__, #a " == " #b, (a), (b))

#define CHECK(cond)	test_check(__FILE__, __LINE__, #cond, (cond) != 0)
#define CHECK_EQ(a, b)		TEST_EQ(a, b)
#define CHECK_NULL(p)							\
	test_eq_ptr(__FILE__, __LINE__, #p " == NULL", (p), nullptr)
#define CHECK_NONNULL(p)						\
	test_check(__FILE__, __LINE__, #p " is NULL", (p) != nullptr)

#define REQUIRE(cond)		do { if (!CHECK(cond)) return; } while (0)
#define REQUIRE_EQ(a, b)	do { if (!CHECK_EQ(a, b)) return; } while (0)
#define REQUIRE_NONNULL(p)	do { if (!CHECK_NONNULL(p)) return; } while (0)

#endif
