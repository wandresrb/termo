/*
 * Cursor scanners for the fixed formats the tree used to sscanf. Each one
 * consumes its input on success and leaves the cursor alone on failure, so a
 * format is a chain of && calls, ending in a *p == '\0' test when the whole
 * string must match. Digits only: no sign, no blanks, no trailing junk taken
 * as a match, which is where sscanf let bad input through.
 */

#include <sys/types.h>

#include <ctype.h>
#include <limits.h>
#include <stdckdint.h>
#include <stdlib.h>
#include <string.h>

#include "termo.h"

bool
scan_lit(const char **s, const char *lit)
{
	size_t	n = strlen(lit);

	if (strncmp(*s, lit, n) != 0)
		return (false);
	*s += n;
	return (true);
}

bool
scan_u(const char **s, u_int *out, u_int max)
{
	const char	*p = *s;
	u_int		 v = 0;

	if (!isdigit((u_char)*p))
		return (false);
	for (; isdigit((u_char)*p); p++) {
		if (ckd_mul(&v, v, 10u) || ckd_add(&v, v, (u_int)(*p - '0')))
			return (false);
	}
	if (v > max)
		return (false);
	*out = v;
	*s = p;
	return (true);
}

bool
scan_i(const char **s, int *out, int min, int max)
{
	const char	*p = *s;
	bool		 neg = (*p == '-');
	u_int		 v;
	int		 i;

	if (neg)
		p++;
	if (!scan_u(&p, &v, INT_MAX))
		return (false);
	i = neg ? -(int)v : (int)v;
	if (i < min || i > max)
		return (false);
	*out = i;
	*s = p;
	return (true);
}

/* Exactly width hex digits, or any run of one to eight when width is 0. */
bool
scan_x(const char **s, u_int *out, u_int width)
{
	const char	*p = *s;
	u_int		 v = 0, n, limit = width != 0 ? width : 8;
	int		 c;

	for (n = 0; n < limit && isxdigit((u_char)p[n]); n++) {
		c = tolower((u_char)p[n]);
		v = (v << 4) | (u_int)(isdigit(c) ? c - '0' : c - 'a' + 10);
	}
	if (n == 0 || (width != 0 && n != width) ||
	    (width == 0 && isxdigit((u_char)p[n])))
		return (false);
	*out = v;
	*s = p + n;
	return (true);
}

bool
scan_d(const char **s, double *out)
{
	char	*end;
	double	 v;

	v = strtod(*s, &end);
	if (end == *s)
		return (false);
	*out = v;
	*s = end;
	return (true);
}
