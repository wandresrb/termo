/*
 * Runner: termo-test [module [case-substring]]. Output is TAP so meson
 * shows every case.
 */

#include <sys/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"
#include "test.h"

static struct test	*tests, **tests_tail = &tests;
static int		 failed;

void
test_register(struct test *t)
{
	*tests_tail = t;
	tests_tail = &t->next;
}

int
test_fail(const char *file, int line, const char *fmt, ...)
{
	va_list	ap;

	failed = 1;
	printf("# %s:%d: ", file, line);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	return (0);
}

int
test_eq_int(const char *file, int line, const char *expr, long long a,
    long long b)
{
	if (a == b)
		return (1);
	return (test_fail(file, line, "%s: got %lld, want %lld", expr, a, b));
}

int
test_eq_uint(const char *file, int line, const char *expr,
    unsigned long long a, unsigned long long b)
{
	if (a == b)
		return (1);
	return (test_fail(file, line, "%s: got %llu, want %llu", expr, a, b));
}

int
test_eq_dbl(const char *file, int line, const char *expr, double a, double b)
{
	if (a == b)
		return (1);
	return (test_fail(file, line, "%s: got %g, want %g", expr, a, b));
}

int
test_eq_str(const char *file, int line, const char *expr, const char *a,
    const char *b)
{
	if (a == b || (a != nullptr && b != nullptr && strcmp(a, b) == 0))
		return (1);
	return (test_fail(file, line, "%s: got %s%s%s, want %s%s%s", expr,
	    a ? "\"" : "", a ? a : "NULL", a ? "\"" : "",
	    b ? "\"" : "", b ? b : "NULL", b ? "\"" : ""));
}

int
test_eq_ptr(const char *file, int line, const char *expr, const void *a,
    const void *b)
{
	if (a == b)
		return (1);
	return (test_fail(file, line, "%s: got %p, want %p", expr, a, b));
}

int
main(int argc, char **argv)
{
	const char	*module = argc > 1 ? argv[1] : nullptr;
	const char	*name = argc > 2 ? argv[2] : nullptr;
	struct test	*t;
	u_int		 n = 0, i = 0, bad = 0;

	termo_test_init();
	if (getenv("TERMO_TEST_LOG") != nullptr) {
		log_add_level();
		log_add_level();
		log_open("test");
	}

	for (t = tests; t != nullptr; t = t->next) {
		if (module != nullptr && strcmp(t->module, module) != 0)
			continue;
		if (name != nullptr && strstr(t->name, name) == nullptr)
			continue;
		n++;
	}
	printf("1..%u\n", n);
	if (n == 0) {
		printf("# no tests match\n");
		return (1);
	}

	for (t = tests; t != nullptr; t = t->next) {
		if (module != nullptr && strcmp(t->module, module) != 0)
			continue;
		if (name != nullptr && strstr(t->name, name) == nullptr)
			continue;
		termo_test_reset();
		failed = 0;
		t->fn();
		bad += failed;
		printf("%s %u - %s/%s\n", failed ? "not ok" : "ok", ++i,
		    t->module, t->name);
		fflush(stdout);
	}
	return (bad != 0);
}
