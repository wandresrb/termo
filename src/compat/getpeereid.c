/* getpeereid(3) for Linux, which exposes the same through SO_PEERCRED. */

#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>

#include "compat.h"

#ifndef SO_PEERCRED
#error "no getpeereid and no SO_PEERCRED: the server cannot check who connects"
#endif

int
getpeereid(int s, uid_t *uid, gid_t *gid)
{
	struct ucred	uc;
	socklen_t	len = sizeof uc;

	if (getsockopt(s, SOL_SOCKET, SO_PEERCRED, &uc, &len) == -1)
		return (-1);
	*uid = uc.uid;
	*gid = uc.gid;
	return (0);
}
