#if !defined(CIO_RESOLV_H)
#define CIO_RESOLV_H

#include <sys/socket.h>
#include <sys/types.h>

enum CIO_ROLE {
    CIO_CLIENT,
    CIO_SERVER
};

/**
 * family - AF_INET, AF_INET6, AF_UNIX, AF_UNSPEC (if AF_INET and AF_INET6 both will do).
 * socktype - SOCK_STREAM, SOCK_DGRAM, 0 - for both
 */
void *cio_new_resolver(const char *addr_string, int port, int family, int socktype,
    enum CIO_ROLE role);

void cio_free_resolver(void *resolver);

/**
 * addr and addrlen - out parameters.
 */
int cio_resolver_next_endpoint(void *resolver, struct sockaddr *addr, int *addrlen);
void cio_resolver_reset_endpoint_iterator(void *resolver);


/**
 * Resolve add_string with ipv4(6) address string locally, without DNS lookup. Creates and returns
 * socket in case of success, -1 otherwise.
 */
int cio_resolve_local(const char *addr_string, int port, int family, struct sockaddr *addr,
    int *addrlen);

#endif /* CIO_RESOLV_H */
