#include "cio_tcp_server.h"
#include "cio_resolver.h"
#include "cio_event_loop.h"
#include "cio_common.h"
#include "cio_tcp_connection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

struct tcp_server_ctx {
    void *event_loop;
    void *user_ctx;
    void (*on_accept)(int fd, void *user_ctx, int ecode);
    int fd;
};

void *cio_new_tcp_server(void *event_loop, void *user_ctx)
{
    struct tcp_server_ctx *sctx;

    sctx = malloc(sizeof(*sctx));
    if (!sctx)
        return NULL;

    memset(sctx, 0, sizeof(*sctx));
    sctx->event_loop = event_loop;
    sctx->user_ctx = user_ctx;

    return sctx;
}

void cio_free_tcp_server(void *tcp_server)
{
    struct tcp_server_ctx *sctx = tcp_server;
    cio_event_loop_remove_fd(sctx->event_loop, sctx->fd);
    free(tcp_server);
}

static void on_accept_impl(void *ctx, int fd, int flags)
{
    struct tcp_server_ctx *sctx = ctx;
    int new_fd;

    if (!(flags & CIO_FLAG_IN)) {
        printf("on_accept poll error: %d\n", flags);
        sctx->on_accept(NULL, sctx->user_ctx, flags);
        return;
    }

    assert(fd == sctx->fd);
    new_fd = accept(fd, NULL, NULL);

    sctx->on_accept(new_fd, sctx->user_ctx, CIO_NO_ERROR);
}

void cio_tcp_server_async_accept(void *tcp_server, const char *addr, int port,
    void (*on_accept)(int fd, void *user_ctx, int ecode))
{
    struct tcp_server_ctx *sctx = tcp_server;
    void *resolver = NULL;
    struct addrinfo ainfo;
    int ecode;

    sctx->on_accept = on_accept;
    resolver = cio_new_resolver(addr, port, AF_UNSPEC, SOCK_STREAM, CIO_SERVER);
    if (!resolver)
        goto fail;

    while (cio_resolver_next_endpoint(resolver, &ainfo) == CIO_NO_ERROR) {
        if ((sctx->fd = socket(ainfo.ai_family, SOCK_STREAM, 0)) == -1) {
            perror("cio_tcp_server_async_accept: socket()");
            continue;
        } else {
            if (bind(sctx->fd, ainfo.ai_addr, sizeof(*ainfo.ai_addr))) {
                perror("cio_tcp_server_async_accept: bind");
                goto fail;
            }
            if (listen(sctx->fd, 128)) {
                perror("cio_tcp_server_async_accept: listen");
                goto fail;
            }
            if (toggle_fd_nonblocking(sctx->fd, 1)) {
                perror("cio_tcp_server_async_accept: toggle non-blocking");
                goto fail;
            }
            if ((ecode = cio_event_loop_add_fd(sctx->event_loop, sctx->fd, CIO_FLAG_IN, sctx, on_accept_impl))) {
                cio_perror(ecode, "cio_tcp_server_async_accept: cio_event_loop_add_fd");
                goto fail;
            }
            return;
        }
    }

fail:
    cio_free_resolver(resolver);
    close(sctx->fd);
    sctx->on_accept(NULL, sctx->user_ctx, CIO_NOT_FOUND_ERROR);
}
