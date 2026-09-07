/* Process-wide state and helpers shared by the server, the client and
 * the tests. */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "termo.h"

struct options	*global_options;	/* server options */
struct options	*global_s_options;	/* session options */
struct options	*global_w_options;	/* window options */
struct environ	*global_environ;

struct timeval	 start_time;
const char	*socket_path;
int		 ptm_fd = -1;
const char	*shell_command;

static int
areshell(const char *shell)
{
	const char	*progname, *ptr;

	if ((ptr = strrchr(shell, '/')) != NULL)
		ptr++;
	else
		ptr = shell;
	progname = getprogname();
	if (*progname == '-')
		progname++;
	if (strcmp(ptr, progname) == 0)
		return (1);
	return (0);
}

int
checkshell(const char *shell)
{
	if (shell == NULL || *shell != '/')
		return (0);
	if (areshell(shell))
		return (0);
	if (access(shell, X_OK) != 0)
		return (0);
	return (1);
}

char *
shell_argv0(const char *shell, int is_login)
{
	const char	*slash, *name;
	char		*argv0;

	slash = strrchr(shell, '/');
	if (slash != NULL && slash[1] != '\0')
		name = slash + 1;
	else
		name = shell;
	if (is_login)
		xasprintf(&argv0, "-%s", name);
	else
		xasprintf(&argv0, "%s", name);
	return (argv0);
}

void
setblocking(int fd, int state)
{
	int mode;

	if ((mode = fcntl(fd, F_GETFL)) != -1) {
		if (!state)
			mode |= O_NONBLOCK;
		else
			mode &= ~O_NONBLOCK;
		fcntl(fd, F_SETFL, mode);
	}
}

uint64_t
get_timer(void)
{
	struct timespec	ts;

	/*
	 * We want a timestamp in milliseconds suitable for time measurement,
	 * so prefer the monotonic clock.
	 */
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		clock_gettime(CLOCK_REALTIME, &ts);
	return ((ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL));
}

char *
clean_name(const char *name, int untrusted)
{
	char	*copy, *cp, *new_name;

	if (!utf8_isvalid(name))
		return (NULL);
	copy = xstrdup(name);
	for (cp = copy; *cp != '\0'; cp++) {
		if (untrusted && cp[0] == '#' && cp[1] == '(')
			*cp = '_';
	}
	utf8_stravis(&new_name, copy, VIS_OCTAL|VIS_CSTYLE|VIS_TAB|VIS_NL);
	free(copy);
	return (new_name);
}

int
check_name(const char *name)
{
	if (!utf8_isvalid(name))
		return (0);
	return (1);
}

const char *
sig2name(int signo)
{
     static char	s[11];

#ifdef HAVE_SYS_SIGNAME
     if (signo > 0 && signo < NSIG)
	     return (sys_signame[signo]);
#endif
     xsnprintf(s, sizeof s, "%d", signo);
     return (s);
}

const char *
find_cwd(void)
{
	char		 resolved1[PATH_MAX], resolved2[PATH_MAX];
	static char	 cwd[PATH_MAX];
	const char	*pwd;

	if (getcwd(cwd, sizeof cwd) == NULL)
		return (NULL);
	if ((pwd = getenv("PWD")) == NULL || *pwd == '\0')
		return (cwd);

	/*
	 * We want to use PWD so that symbolic links are maintained,
	 * but only if it matches the actual working directory.
	 */
	if (realpath(pwd, resolved1) == NULL)
		return (cwd);
	if (realpath(cwd, resolved2) == NULL)
		return (cwd);
	if (strcmp(resolved1, resolved2) != 0)
		return (cwd);
	return (pwd);
}

const char *
find_home(void)
{
	struct passwd		*pw;
	static const char	*home;

	if (home != NULL)
		return (home);

	home = getenv("HOME");
	if (home == NULL || *home == '\0') {
		pw = getpwuid(getuid());
		if (pw != NULL)
			home = xstrdup(pw->pw_dir);
		else
			home = NULL;
	}

	return (home);
}

const char *
getversion(void)
{
	return (TMUX_VERSION);
}
