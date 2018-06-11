#if !defined(CIO_RESOLV_H)
#define CIO_RESOLV_H

#include <sys/socket.h>
#include <sys/types.h>

/**
 * Resolves addr_string and port to the out arguments addr and addrlen and creates socket of family
 * kind.
 * family - might be AF_UNIX, AF_INET, AF_INET6.
 * Returns created socket or -1 on error.
 */
int cio_resolve(const char *addr_string, int port, int family, struct sockaddr *addr, int *addrlen);

#endif /* CIO_RESOLV_H */
