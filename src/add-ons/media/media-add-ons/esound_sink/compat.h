
#ifndef _COMPAT_H
#define _COMPAT_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#if defined(__HAIKU__) || defined(BONE_VERSION) || defined(__linux__)
# define closesocket close
#endif
/* net_server */

#endif /* _COMPAT_H */
