#include "cio_tcp_server.h"
#include "cio_resolver.h"
#include "cio_event_loop.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

struct tcp_server_ctx {
    void *event_loop;
    void *user_ctx;
    void (*on_accept)(void *connection, void *user_ctx, int ecode);
    int fd;
};

void *cio_new_tcp_server(void *event_loop, void *user_ctx,
    void (*on_accept)(void *connection, void *user_ctx, int ecode))
{
    struct tcp_server_ctx *sctx;

    sctx = malloc(sizeof(*sctx));
    if (!sctx)
        return NULL;

    memset(sctx, 0, sizeof(*sctx));
    sctx->event_loop = event_loop;
    sctx->user_ctx = user_ctx;
    sctx->on_accept = on_accept;

    return sctx;
}

void cio_free_tcp_server(void *tcp_server)
{
    struct tcp_server_ctx *sctx = tcp_server;
    cio_event_loop_remove_fd(sctx->event_loop, sctx->fd);
    free(tcp_server);
}

void cio_tcp_server_serve(void *tcp_server, const char *addr, int port)
{
    struct tcp_server_ctx *sctx = tcp_server;
    void *resolver = NULL;
    struct addrinfo ainfo;

    resolver = cio_new_resolver(addr, port, AF_UNSPEC, SOCK_STREAM, CIO_SERVER);
    if (!resolver)
        goto fail;

    while (cio_resolver_next_endpoint(resolver, &ainfo) == CIO_NO_ERROR) {
        if ((sctx->fd = socket(ainfo.ai_family, SOCK_STREAM, 0)) == -1) {
            perror("cio_tcp_server_serve, socket()");
            continue;
        } else {
            if (bind(sctx->fd, ainfo.ai_addr, sizeof(*ainfo.ai_addr))) {

            }
        }
    }


    return;

fail:
    cio_free_resolver(resolver);
    sctx->on_accept(NULL, sctx->user_ctx, CIO_NOT_FOUND_ERROR);
}
