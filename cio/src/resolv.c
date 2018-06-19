#include "resolv.h"
#include "common.h"
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
    int socktype;
};

void *cio_new_resolver(const char *addr_string, int port, int family, int socktype,
    enum CIO_ROLE role)
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
    rctx->socktype = socktype;

    snprintf(port_buf, sizeof(port_buf), "%d", port);
    memset(&hints, 0, sizeof(hints));

    if (socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
        goto fail;

    switch (family) {
    case AF_INET:
    case AF_INET6:
    case AF_UNSPEC:
        hints.ai_family = family;
        hints.ai_socktype = socktype;
        hints.ai_protocol = 0;

        if (role == CIO_SERVER) {
            hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
        } else if (role == CIO_CLIENT) {
            hints.ai_flags = AI_ADDRCONFIG;
        } else {
            goto fail;
        }

        if (getaddrinfo(addr_string, port_buf, &hints, &rctx->root))
            goto fail;

        rctx->current = rctx->root;
        break;

    case AF_UNIX:
        memset(&un_addr, 0, sizeof(un_addr));
        strncpy(un_addr.sun_path, addr_string, sizeof(un_addr.sun_path) - 1);

        rctx->root = malloc(sizeof(*rctx->root));
        if (!rctx->root)
            goto fail;

        memset(rctx->root, 0, sizeof(*rctx->root));

        rctx->root->ai_addr = malloc(sizeof(*rctx->root->ai_addr));
        if (!rctx->root->ai_addr) {
            free(rctx->root);
            goto fail;
        }

        memcpy(rctx->root->ai_addr, &un_addr, sizeof(un_addr));
        rctx->root->ai_addrlen = sizeof(un_addr);

        break;

    default:
        goto fail;
    }

    return rctx;

fail:
    free(rctx);
    perror("cio_new_resolver");

    return NULL;
}

void cio_free_resolver(void *resolver)
{
    struct resolver_ctx *rctx = (struct resolver_ctx *) resolver;

    if (rctx->root)
        freeaddrinfo(rctx->root);

    free(rctx);
}

int cio_resolver_next_endpoint(void *resolver, struct sockaddr *addr, int *addrlen)
{
    struct resolver_ctx *rctx = (struct resolver_ctx *) resolver;

     while (rctx->current) {
        if (rctx->current->ai_socktype == rctx->socktype || rctx->socktype == 0) {
            memcpy(addr, rctx->current->ai_addr, sizeof(*addr));
            *addrlen = rctx->current->ai_addrlen;
            rctx->current = rctx->current->ai_next;
            return CIO_NO_ERROR;
        }
        rctx->current = rctx->current->ai_next;
     }

     return CIO_NOT_FOUND_ERROR;
}

void cio_resolver_reset_endpoint_iterator(void *resolver)
{
    struct resolver_ctx *rctx = (struct resolver_ctx *) resolver;
    rctx->current = rctx->root;
}

int cio_resolve_local(const char *addr_string, int port, int family, struct sockaddr *addr,
    int *addrlen)
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

