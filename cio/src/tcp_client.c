#include "tcp_client.h"
#include "event_loop.h"
#include "resolv.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>

enum connection_state {
    CIO_CS_INITIAL,
    CIO_CS_CONNECTING,
    CIO_CS_CONNECTED,
    CIO_CS_ERROR
};

/* This struct is used to inform all currently alive contexts (which reside in the event_loop) that
 * main context is destroyed in order not to crash on callback execution after tcp_client
 * destruction.
 */
struct async_ctx {
    void *ctx;
    void (*on_destroy)(void *ctx);
    struct async_ctx *next;
};

static void *new_async_ctx(struct async_ctx **root, void *ctx, void (*on_destroy)(void *ctx))
{
    struct async_ctx *asctx;
    struct async_ctx *cur = *root;

    asctx = malloc(sizeof(*asctx));
    if (!asctx)
        return NULL;

    asctx->ctx = ctx;
    asctx->on_destroy = on_destroy;
    asctx->next = NULL;

    if (!*root) {
        *root = asctx;
    } else {
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = asctx;
    }

    return asctx;
}

static void free_async_ctx(struct async_ctx *root)
{
    struct async_ctx *cur = root;

    if (!root)
        return;

    while (root) {
        cur = root;
        root = root->next;
        cur->on_destroy(cur->ctx);
        free(cur);
    }
}

struct tcp_client_ctx {
    void *event_loop;
    void *ctx;
    int fd;
    enum connection_state cstate;
    struct async_ctx *asctx;
};

void *cio_new_tcp_client(void *event_loop, void *ctx)
{
    struct tcp_client_ctx *tctx;

    tctx = malloc(sizeof(*tctx));
    if (!tctx)
        goto fail;

    tctx->event_loop = event_loop;
    tctx->ctx = ctx;
    tctx->fd = -1;
    tctx->cstate = CIO_CS_INITIAL;
    tctx->asctx = NULL;

    return tctx;

fail:
    perror("cio_new_tcp_client");
    free(tctx);
    return NULL;
}

static void free_tcp_client_impl(void *ctx)
{
    struct tcp_client_ctx *tctx = ctx;

    free_async_ctx(tctx->asctx);
    free(tctx);
}

void cio_free_tcp_client(void *tcp_client)
{
    struct tcp_client_ctx *tctx = tcp_client;
    cio_event_loop_add_timer(tctx->event_loop, 0, tctx, free_tcp_client_impl);
}

struct connect_ctx {
    struct tcp_client_ctx *tcp_client;
    void (*on_connect)(void *ctx, int ecode);
    void *resolver;
};

static void connect_ctx_try_next(struct connect_ctx *tctx);
static void free_connect_ctx(struct connect_ctx *cctx);

static void connect_ctx_on_connect_cb(void *ctx, int fd, int flags)
{
    struct connect_ctx *cctx = ctx;
    struct tcp_client_ctx *tctx = cctx->tcp_client;

    if (!tctx) {
        free_connect_ctx(cctx);
        return;
    }

    assert(fd == tctx->fd);
    if (flags & CIO_FLAG_OUT) {
        cio_event_loop_remove_fd(tctx->event_loop, tctx->fd);
        cctx->on_connect(tctx->ctx, CIO_NO_ERROR);
        free_connect_ctx(cctx);
        return;
    }

    fprintf(stdout, "on_connect_cb, error flags: %d\n", flags);
    connect_ctx_try_next(cctx);
}

static void free_connect_ctx(struct connect_ctx *cctx)
{
    struct tcp_client_ctx *tctx = cctx->tcp_client;
    struct async_ctx *asctx = tctx->asctx, *tmp_asctx = NULL;

    if (tctx) {
        while (asctx) {
            if (asctx->ctx == cctx) {
                if (tmp_asctx) {
                    tmp_asctx->next = asctx->next;
                    free(asctx);
                    break;
                } else {
                    tctx->asctx = asctx->next;
                }
            } else {
                tmp_asctx = asctx;
                asctx = asctx->next;
            }
        }
    }

    cio_free_resolver(cctx->resolver);
    free(cctx);
}

static void connect_ctx_on_destroy(void *ctx)
{
    struct connect_ctx *cctx = ctx;
    cctx->tcp_client = NULL;
}

static struct connect_ctx *new_connect_ctx(struct tcp_client_ctx *tcp_client, const char *addr,
    int port, void (*on_connect)(void *ctx, int ecode))
{
    struct connect_ctx *cctx;

    cctx = malloc(sizeof(*cctx));
    if (!cctx)
        return NULL;

    cctx->tcp_client = tcp_client;
    cctx->on_connect = on_connect;
    cctx->resolver = cio_new_resolver(addr, port, AF_UNSPEC, SOCK_STREAM,
        CIO_CLIENT);
    if (!cctx->resolver) {
        free(cctx);
        return NULL;
    }

    if (!new_async_ctx(&tcp_client->asctx, cctx, connect_ctx_on_destroy)) {
        free_connect_ctx(cctx);
        return NULL;
    }

    return cctx;
}

static void connect_ctx_try_next(struct connect_ctx *cctx)
{
    int cio_ecode = CIO_NO_ERROR;
    int system_ecode = 0;
    struct tcp_client_ctx *tctx = cctx->tcp_client;
    struct addrinfo ainfo;

    assert(tctx->cstate == CIO_CS_CONNECTING);
    if ((cio_ecode = cio_resolver_next_endpoint(cctx->resolver, &ainfo)))
        goto fail;

    close(tctx->fd);
    if ((tctx->fd = socket(ainfo.ai_family, ainfo.ai_socktype, 0)) == -1) {
        system_ecode = errno;
        goto fail;
    }

    if ((system_ecode = toggle_fd_nonblocking(tctx->fd, 1)))
        goto fail;

    if (connect(tctx->fd, ainfo.ai_addr, ainfo.ai_addrlen))
        system_ecode = errno;

    switch (system_ecode) {
    case 0:
        tctx->cstate = CIO_CS_CONNECTED;
        cctx->on_connect(tctx->ctx, CIO_NO_ERROR);
        free_connect_ctx(cctx);
        break;
    case EINPROGRESS:
        cio_event_loop_remove_fd(tctx->event_loop, tctx->fd);
        cio_ecode = cio_event_loop_add_fd(tctx->event_loop, tctx->fd, CIO_FLAG_OUT,
            cctx, connect_ctx_on_connect_cb);
        if (cio_ecode)
            goto fail;
        break;
    default:
        perror("try_next_endpoint, connect");
        connect_ctx_try_next(cctx);
        break;
    }

    return;

fail:
    if (cio_ecode)
        cio_perror(cio_ecode, "try_next_endpoint");
    if (system_ecode)
        perror("try_next_endpoint");

    tctx->cstate = CIO_CS_ERROR;
    close(tctx->fd);
    cctx->on_connect(tctx->ctx, cio_ecode ? cio_ecode : CIO_UNKNOWN_ERROR);
    free_connect_ctx(cctx);
}

static void async_connect_impl(void *ctx)
{
    struct connect_ctx *cctx = ctx;
    struct tcp_client_ctx *tctx = cctx->tcp_client;

    if (!tctx) {
        free_connect_ctx(cctx);
        return;
    }

    switch (tctx->cstate) {
    case CIO_CS_CONNECTED:
    case CIO_CS_CONNECTING:
        cctx->on_connect(tctx->ctx, CIO_WRONG_STATE_ERROR);
        free_connect_ctx(cctx);
        return;
    case CIO_CS_INITIAL:
    case CIO_CS_ERROR:
        tctx->cstate = CIO_CS_CONNECTING;
        break;
    default:
        assert(0);
    }

    connect_ctx_try_next(cctx);
}

void cio_tcp_client_async_connect(void *tcp_client, const char *addr, int port,
    void (*on_connect)(void *ctx, int ecode))
{
    struct connect_ctx *cctx = new_connect_ctx(tcp_client, addr, port, on_connect);
    struct tcp_client_ctx *tctx = tcp_client;

    if (!cctx) {
        cio_perror(CIO_ALLOC_ERROR, "cio_tcp_client_async_connect");
        on_connect(tctx->ctx, CIO_ALLOC_ERROR);
    }

    cio_event_loop_add_timer(tctx->event_loop, 0, cctx, async_connect_impl);
}

struct write_ctx {
    struct tcp_client_ctx *tcp_client;
    void (*on_write)(void *ctx, int ecode, int written_len);
    const void *data;
    int len;
    int written;
};

static void do_write(struct write_ctx *wctx)
{

}

static void async_write_impl(void *ctx)
{
    struct write_ctx *wctx = ctx;
    struct tcp_client_ctx *tctx = wctx->tcp_client;

    switch (tctx->cstate) {
    case CIO_CS_CONNECTED:
        break;
    case CIO_CS_CONNECTING:
    case CIO_CS_INITIAL:
    case CIO_CS_ERROR:
        free(wctx);
        fprintf(stdout, "async_write_impl: wrong state\n");
        return;
    default:
        assert(0);
    }

    do_write(wctx);
}

void cio_tcp_client_async_write(void *tcp_client, const void *data, int len,
    void (*on_write)(void *ctx, int ecode, int written_len))
{
    struct write_ctx *wctx;
    struct tcp_client_ctx *tctx = tcp_client;
    int ecode = 0;

    wctx = malloc(sizeof(*wctx));
    if (!wctx) {
        ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    wctx->tcp_client = tcp_client;
    wctx->on_write = on_write;
    wctx->data = data;
    wctx->len = len;
    wctx->written = 0;

    cio_event_loop_add_timer(tctx->event_loop, 0, wctx, async_write_impl);

fail:
    cio_perror(ecode, "cio_tcp_client_async_connect");
    on_write(tctx->ctx, ecode, 0);
}
