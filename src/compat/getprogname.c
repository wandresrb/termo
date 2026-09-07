/* getprogname(3) for glibc and musl. */

#include <sys/types.h>

#include <errno.h>

#include "compat.h"

const char *
getprogname(void)
{
	return (program_invocation_short_name);
}
