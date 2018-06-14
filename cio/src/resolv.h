#if !defined(CIO_RESOLV_H)
#define CIO_RESOLV_H

#include <sys/socket.h>
#include <sys/types.h>

enum CIO_FAMILY {
    CIO_UNIX, /* AF_UNIX */
    CIO_INET /* AF_INET or AF_INET6 */
};

enum CIO_ROLE {
    CIO_CLIENT,
    CIO_SERVER
};

/**
 * sock_type - SOCK_STREAM, SOCK_DGRAM, 0 - for both.
 */
void *cio_new_resolver(const char *addr_string, int port, enum CIO_FAMILY family,
    enum CIO_ROLE role, int sock_type);

void cio_free_resolver(void *resolver);

/**
 * addr and addrlen - out parameters.
 */
int cio_resolver_next_endpoint(void *resolver, struct sockaddr **addr, int *addrlen);


int cio_resolve(const char *addr_string, int port, int family, struct sockaddr *addr, int *addrlen);

#endif /* CIO_RESOLV_H */
