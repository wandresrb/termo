/* closefrom(3) for the one target without it, macOS; Linux musl also lacks it. */

#include <sys/types.h>

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#endif

#include "compat.h"

#ifdef __APPLE__
static int
closefrom_pidinfo(int lowfd)
{
	struct proc_fdinfo	*fds;
	int			 n, i, size;

	size = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
	if (size <= 0)
		return (-1);
	fds = malloc(size);
	if (fds == nullptr)
		return (-1);
	n = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, fds, size);
	if (n <= 0) {
		free(fds);
		return (-1);
	}
	for (i = 0; i < n / (int)sizeof *fds; i++) {
		if (fds[i].proc_fd >= lowfd)
			close(fds[i].proc_fd);
	}
	free(fds);
	return (0);
}
#endif

#ifdef __linux__
static int
closefrom_procfs(int lowfd)
{
	DIR		*dir;
	struct dirent	*de;
	int		 dfd, fd;

	if ((dir = opendir("/proc/self/fd")) == nullptr)
		return (-1);
	dfd = dirfd(dir);
	while ((de = readdir(dir)) != nullptr) {
		fd = atoi(de->d_name);
		if (fd >= lowfd && fd != dfd && de->d_name[0] != '.')
			close(fd);
	}
	closedir(dir);
	return (0);
}
#endif

void
closefrom(int lowfd)
{
	long	maxfd, fd;

#ifdef __APPLE__
	if (closefrom_pidinfo(lowfd) == 0)
		return;
#endif
#ifdef __linux__
	if (closefrom_procfs(lowfd) == 0)
		return;
#endif
	maxfd = sysconf(_SC_OPEN_MAX);
	if (maxfd < 0)
		maxfd = 1024;
	for (fd = lowfd; fd < maxfd; fd++)
		close((int)fd);
}
