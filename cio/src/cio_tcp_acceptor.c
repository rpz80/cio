#include "cio_tcp_acceptor.h"
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

struct tcp_acceptor_ctx {
    void *event_loop;
    void *user_ctx;
    void (*on_accept)(int fd, void *user_ctx, int ecode);
    int fd;
};

void *cio_new_tcp_acceptor(void *event_loop, void *user_ctx)
{
    struct tcp_acceptor_ctx *sctx;

    sctx = malloc(sizeof(*sctx));
    if (!sctx)
        return NULL;

    memset(sctx, 0, sizeof(*sctx));
    sctx->event_loop = event_loop;
    sctx->user_ctx = user_ctx;
    sctx->fd = -1;

    return sctx;
}

static void free_tcp_acceptor_impl(void *ctx)
{
    struct tcp_acceptor_ctx *acceptor_ctx;
    struct completion_ctx *completion_ctx = NULL;
    enum obj_type type;
    
    type = *((enum obj_type *) ctx);
    switch (type) {
        case ACCEPTOR:
            acceptor_ctx = ctx;
            break;
        case COMPLETION:
            completion_ctx = ctx;
            acceptor_ctx = completion_ctx->wrapped_ctx;
            break;
        default:
            assert(0);
    }
    
    cio_event_loop_remove_fd(acceptor_ctx->event_loop, acceptor_ctx->fd);
    close(acceptor_ctx->fd);
    free(acceptor_ctx);
 
    pthread_mutex_lock(&completion_ctx->mutex);
    if (type == COMPLETION)
        pthread_cond_signal(&completion_ctx->cond);
    pthread_mutex_unlock(&completion_ctx->mutex);
}

void cio_free_tcp_acceptor_async(void *tcp_acceptor)
{
    struct tcp_acceptor_ctx *acceptor_ctx = tcp_acceptor;

    if (!acceptor_ctx)
        return;
    
    cio_event_loop_post(acceptor_ctx->event_loop, 0, acceptor_ctx, free_tcp_acceptor_impl);
}

void cio_free_tcp_acceptor_sync(void *tcp_acceptor)
{
    struct tcp_acceptor_ctx *acceptor_ctx = tcp_acceptor;
    struct completion_ctx *completion_ctx;
    int ecode;
    
    if (!acceptor_ctx)
        return;
    
    if (!(completion_ctx = new_completion_ctx(acceptor_ctx)))
        goto fail;
    
    if ((ecode = completion_ctx_post_and_wait(completion_ctx, acceptor_ctx->event_loop,
                                              free_tcp_acceptor_impl, 0))) {
        errno = ecode;
        goto fail;
    }
    
    goto finally;
    
fail:
    perror("cio_free_tcp_acceptor_sync");
    
finally:
    free_completion_ctx(completion_ctx);
}

static void on_accept_impl(void *ctx, int fd, int flags)
{
    struct tcp_acceptor_ctx *sctx = ctx;
    int new_fd;

    if (!(flags & CIO_FLAG_IN)) {
        printf("on_accept poll error: %d\n", flags);
        sctx->on_accept(-1, sctx->user_ctx, flags);
        return;
    }

    assert(fd == sctx->fd);
    new_fd = accept(fd, NULL, NULL);

    sctx->on_accept(new_fd, sctx->user_ctx, CIO_NO_ERROR);
}

void cio_tcp_acceptor_async_accept(void *tcp_server, const char *addr, int port,
    void (*on_accept)(int fd, void *user_ctx, int ecode))
{
    struct tcp_acceptor_ctx *sctx = tcp_server;
    void *resolver = NULL;
    struct addrinfo ainfo;
    int ecode;

    sctx->on_accept = on_accept;
    resolver = cio_new_resolver(addr, port, AF_UNSPEC, SOCK_STREAM, CIO_SERVER);
    if (!resolver)
        goto fail;

    while (cio_resolver_next_endpoint(resolver, &ainfo) == CIO_NO_ERROR) {
        if ((sctx->fd = socket(ainfo.ai_family, SOCK_STREAM, 0)) == -1) {
            perror("cio_acceptor_async_accept: socket()");
            continue;
        } else {
            if (setsockopt(sctx->fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int))) {
                perror("cio_acceptor_async_accept: setsockopt");
                goto fail;
            }
            if (bind(sctx->fd, ainfo.ai_addr, sizeof(*ainfo.ai_addr))) {
                perror("cio_acceptor_async_accept: bind");
                goto fail;
            }
            if (listen(sctx->fd, 128)) {
                perror("cio_acceptor_async_accept: listen");
                goto fail;
            }
            if (toggle_fd_nonblocking(sctx->fd, 1)) {
                perror("cio_acceptor_async_accept: toggle non-blocking");
                goto fail;
            }
            if ((ecode = cio_event_loop_add_fd(sctx->event_loop, sctx->fd, CIO_FLAG_IN, sctx, on_accept_impl))) {
                cio_perror(ecode, "cio_acceptor_async_accept: cio_event_loop_add_fd");
                goto fail;
            }
            cio_free_resolver(resolver);
            return;
        }
    }

fail:
    cio_free_resolver(resolver);
    close(sctx->fd);
    sctx->on_accept(-1, sctx->user_ctx, CIO_NOT_FOUND_ERROR);
}
