/* setproctitle(3) where it is missing: prctl on Linux, nothing on macOS. */

#include <sys/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "compat.h"

void
setproctitle([[maybe_unused]] const char *fmt, ...)
{
#ifdef __linux__
	char	title[16], name[16], *cp;
	va_list	ap;
	int	used;

	va_start(ap, fmt);
	used = vsnprintf(title, sizeof title, fmt, ap);
	va_end(ap);
	if (used <= 0)
		return;
	if ((cp = strchr(title, ' ')) != nullptr)
		*cp = '\0';
	strlcpy(name, getprogname(), sizeof name);
	strlcat(name, ": ", sizeof name);
	strlcat(name, title, sizeof name);
	prctl(PR_SET_NAME, name);
#endif
}
