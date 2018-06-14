#include "resolv.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct resolver_ctx {
    struct addrinfo *root;
    struct addrinfo *current;
};

void *cio_new_resolver(const char *addr_string, int port, enum CIO_FAMILY family,
    enum CIO_ROLE role, int sock_type)
{
    struct resolver_ctx *rctx;
    char port_buf[16];
    struct addrinfo hints;
    struct sockaddr_un un_addr;

    rctx = malloc(sizeof(*rctx));
    if (!rctx)
        goto fail;

    if (port < 0 || port > 65535)
        goto fail;

    rctx->current = NULL;
    rctx->root = NULL;
    snprintf(port_buf, "%d", port);

    switch (family) {
    case CIO_INET:
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = 0;
        hints.ai_protocol = 0;
        break;

    case CIO_UNIX:
        memset(&un_addr, 0, sizeof(un_addr));

        break;
    }

    return rctx;

fail:
    free(rctx);
    perror("cio_new_resolver");

    return NULL;
}

void cio_free_resolver(void *resolver)
{

}

/**
 * addr and addrlen - out parameters.
 */
int cio_resolver_next_endpoint(void *resolver, struct sockaddr **addr, int *addrlen);

int cio_resolve(const char *addr_string, int port, int family, struct sockaddr *addr, int *addrlen)
{
    struct sockaddr_in ipv4_addr;
    struct sockaddr_in6 ipv6_addr;
    struct sockaddr_un un_addr;
    int fd;

    switch (family) {
    case AF_INET6:
        memset(&ipv6_addr, 0, sizeof(ipv6_addr));
        if (inet_pton(AF_INET6, addr_string, &ipv6_addr.sin6_addr) == -1) {
            perror("inet_pton6");
            return -1;
        }
        ipv6_addr.sin6_family = AF_INET6;
        ipv6_addr.sin6_port = htons(port);
        memcpy(addr, &ipv6_addr, sizeof(ipv6_addr));
        *addrlen = sizeof(ipv6_addr);
        break;
    case AF_INET:
        memset(&ipv4_addr, 0, sizeof(ipv4_addr));
        if (inet_pton(AF_INET, addr_string, &ipv4_addr.sin_addr) == -1) {
            perror("inet_pton4");
            return -1;
        }
        ipv4_addr.sin_family = AF_INET;
        ipv4_addr.sin_port = htons(port);
        memcpy(addr, &ipv4_addr, sizeof(ipv4_addr));
        *addrlen = sizeof(ipv4_addr);
        break;
    case AF_UNIX:
        memset(&un_addr, 0, sizeof(un_addr));
        un_addr.sun_family = AF_UNIX;
        strncpy(un_addr.sun_path, addr_string, sizeof(un_addr.sun_path) - 1);
        memcpy(addr, &un_addr, sizeof(un_addr));
        *addrlen = sizeof(un_addr);
        break;
    default:
        perror("invalid family");
        return -1;
    }

    fd = socket(SOCK_STREAM, family, 0);
    return fd;
}

