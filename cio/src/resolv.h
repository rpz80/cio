#if !defined(CIO_RESOLV_H)
#define CIO_RESOLV_H

#include <sys/socket.h>
#include <sys/types.h>

enum CIO_ROLE {
    CIO_CLIENT,
    CIO_SERVER
};

/**
 * family - AF_INET, AF_INET6, AF_UNIX.
 */
void *cio_new_resolver(const char *addr_string, int port, int family, enum CIO_ROLE role);

void cio_free_resolver(void *resolver);

/**
 * addr and addrlen - out parameters.
 */
int cio_resolver_next_endpoint(void *resolver, struct sockaddr *addr, int *addrlen);
void cio_resolver_reset_endpoint_iterator(void *resolver);


/**
 * Resolve add_string with ipv4(6) address string locally, without DNS lookup.
 */
int cio_resolve_local(const char *addr_string, int port, int family, struct sockaddr *addr,
    int *addrlen);

#endif /* CIO_RESOLV_H */
