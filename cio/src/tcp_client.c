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
struct wrapper_ctx {
    void *posted_ctx;
    void (*on_destroy)(void *ctx);
    struct wrapper_ctx *next;
};

static void *new_wrapper_ctx(struct wrapper_ctx **root, void *ctx, void (*on_destroy)(void *ctx))
{
    struct wrapper_ctx *new_wrapper_ctx;
    struct wrapper_ctx *cur = *root;

    new_wrapper_ctx = malloc(sizeof(*new_wrapper_ctx));
    if (!new_wrapper_ctx)
        return NULL;

    new_wrapper_ctx->posted_ctx = ctx;
    new_wrapper_ctx->on_destroy = on_destroy;
    new_wrapper_ctx->next = NULL;

    if (!*root) {
        *root = new_wrapper_ctx;
    } else {
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = new_wrapper_ctx;
    }

    return new_wrapper_ctx;
}

static void free_all_wrappers(struct wrapper_ctx *root_wrapper_ctx)
{
    struct wrapper_ctx *cur_wrapper_ctx = root_wrapper_ctx;

    if (!root_wrapper_ctx)
        return;

    while (root_wrapper_ctx) {
        cur_wrapper_ctx = root_wrapper_ctx;
        root_wrapper_ctx = root_wrapper_ctx->next;
        cur_wrapper_ctx->on_destroy(cur_wrapper_ctx->posted_ctx);
        free(cur_wrapper_ctx);
    }
}

struct tcp_client_ctx {
    void *event_loop;
    void *user_ctx;
    int fd;
    enum connection_state cstate;
    struct wrapper_ctx *wrapper_ctx;
};

void *cio_new_tcp_client(void *event_loop, void *ctx)
{
    struct tcp_client_ctx *tctx;

    tctx = malloc(sizeof(*tctx));
    if (!tctx)
        goto fail;

    tctx->event_loop = event_loop;
    tctx->user_ctx = ctx;
    tctx->fd = -1;
    tctx->cstate = CIO_CS_INITIAL;
    tctx->wrapper_ctx = NULL;

    return tctx;

fail:
    perror("cio_new_tcp_client");
    free(tctx);
    return NULL;
}

static void free_tcp_client_impl(void *ctx)
{
    struct tcp_client_ctx *tcp_client_ctx = ctx;

    free_all_wrappers(tcp_client_ctx->wrapper_ctx);
    free(tcp_client_ctx);
}

void cio_free_tcp_client(void *tcp_client)
{
    struct tcp_client_ctx *tctx = tcp_client;
    cio_event_loop_add_timer(tctx->event_loop, 0, tctx, free_tcp_client_impl);
}

static void tcp_client_remove_wrapper_by_posted_ctx(struct tcp_client_ctx *tcp_client_ctx,
    void *posted_ctx)
{
    struct wrapper_ctx *wrapper_ctx, *tmp_wrapper_ctx;

    if (!tcp_client_ctx)
        return;

    wrapper_ctx = tcp_client_ctx->wrapper_ctx;
    tmp_wrapper_ctx = NULL;

    while (wrapper_ctx) {
        if (wrapper_ctx->posted_ctx == posted_ctx) {
            if (tmp_wrapper_ctx) {
                tmp_wrapper_ctx->next = wrapper_ctx->next;
                free(wrapper_ctx);
                break;
            } else {
                tcp_client_ctx->wrapper_ctx = wrapper_ctx->next;
            }
        } else {
            tmp_wrapper_ctx = wrapper_ctx;
            wrapper_ctx = wrapper_ctx->next;
        }
    }
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
    struct connect_ctx *connect_ctx = ctx;
    struct tcp_client_ctx *tcp_client_ctx = connect_ctx->tcp_client;

    if (!tcp_client_ctx) {
        free_connect_ctx(connect_ctx);
        return;
    }

    assert(fd == tcp_client_ctx->fd);
    if (flags & CIO_FLAG_OUT) {
        cio_event_loop_remove_fd(tcp_client_ctx->event_loop, tcp_client_ctx->fd);
        connect_ctx->on_connect(tcp_client_ctx->user_ctx, CIO_NO_ERROR);
        free_connect_ctx(connect_ctx);
        return;
    }

    fprintf(stdout, "on_connect_cb, error flags: %d\n", flags);
    connect_ctx_try_next(connect_ctx);
}

static void free_connect_ctx(struct connect_ctx *connect_ctx)
{
    struct tcp_client_ctx *tcp_client_ctx = connect_ctx->tcp_client;

    tcp_client_remove_wrapper_by_posted_ctx(tcp_client_ctx, connect_ctx);
    cio_free_resolver(connect_ctx->resolver);
    free(connect_ctx);
}

static void connect_ctx_on_destroy(void *ctx)
{
    struct connect_ctx *connect_ctx = ctx;
    connect_ctx->tcp_client = NULL;
}

static struct connect_ctx *new_connect_ctx(struct tcp_client_ctx *tcp_client, const char *addr,
    int port, void (*on_connect)(void *ctx, int ecode))
{
    struct connect_ctx *connect_ctx;

    connect_ctx = malloc(sizeof(*connect_ctx));
    if (!connect_ctx)
        return NULL;

    connect_ctx->tcp_client = tcp_client;
    connect_ctx->on_connect = on_connect;
    connect_ctx->resolver = cio_new_resolver(addr, port, AF_UNSPEC, SOCK_STREAM,
        CIO_CLIENT);
    if (!connect_ctx->resolver) {
        free(connect_ctx);
        return NULL;
    }

    if (!new_wrapper_ctx(&tcp_client->wrapper_ctx, connect_ctx, connect_ctx_on_destroy)) {
        free_connect_ctx(connect_ctx);
        return NULL;
    }

    return connect_ctx;
}

static void connect_ctx_try_next(struct connect_ctx *connect_ctx)
{
    int cio_ecode = CIO_NO_ERROR;
    int system_ecode = 0;
    struct tcp_client_ctx *tcp_client_ctx = connect_ctx->tcp_client;
    struct addrinfo ainfo;

    assert(tcp_client_ctx->cstate == CIO_CS_CONNECTING);
    if ((cio_ecode = cio_resolver_next_endpoint(connect_ctx->resolver, &ainfo)))
        goto fail;

    close(tcp_client_ctx->fd);
    if ((tcp_client_ctx->fd = socket(ainfo.ai_family, ainfo.ai_socktype, 0)) == -1) {
        system_ecode = errno;
        goto fail;
    }

    if ((system_ecode = toggle_fd_nonblocking(tcp_client_ctx->fd, 1)))
        goto fail;

    if (connect(tcp_client_ctx->fd, ainfo.ai_addr, ainfo.ai_addrlen))
        system_ecode = errno;

    switch (system_ecode) {
    case 0:
        tcp_client_ctx->cstate = CIO_CS_CONNECTED;
        connect_ctx->on_connect(tcp_client_ctx->user_ctx, CIO_NO_ERROR);
        free_connect_ctx(connect_ctx);
        break;
    case EINPROGRESS:
        cio_event_loop_remove_fd(tcp_client_ctx->event_loop, tcp_client_ctx->fd);
        cio_ecode = cio_event_loop_add_fd(tcp_client_ctx->event_loop, tcp_client_ctx->fd, CIO_FLAG_OUT,
            connect_ctx, connect_ctx_on_connect_cb);
        if (cio_ecode)
            goto fail;
        break;
    default:
        perror("try_next_endpoint, connect");
        connect_ctx_try_next(connect_ctx);
        break;
    }

    return;

fail:
    if (cio_ecode)
        cio_perror(cio_ecode, "try_next_endpoint");
    if (system_ecode)
        perror("try_next_endpoint");

    tcp_client_ctx->cstate = CIO_CS_ERROR;
    close(tcp_client_ctx->fd);
    connect_ctx->on_connect(tcp_client_ctx->user_ctx, cio_ecode ? cio_ecode : CIO_UNKNOWN_ERROR);
    free_connect_ctx(connect_ctx);
}

static void async_connect_impl(void *ctx)
{
    struct connect_ctx *connect_ctx = ctx;
    struct tcp_client_ctx *tcp_client_ctx = connect_ctx->tcp_client;

    if (!tcp_client_ctx) {
        free_connect_ctx(connect_ctx);
        return;
    }

    switch (tcp_client_ctx->cstate) {
    case CIO_CS_CONNECTED:
    case CIO_CS_CONNECTING:
        connect_ctx->on_connect(tcp_client_ctx->user_ctx, CIO_WRONG_STATE_ERROR);
        free_connect_ctx(connect_ctx);
        return;
    case CIO_CS_INITIAL:
    case CIO_CS_ERROR:
        tcp_client_ctx->cstate = CIO_CS_CONNECTING;
        break;
    default:
        assert(0);
    }

    connect_ctx_try_next(connect_ctx);
}

void cio_tcp_client_async_connect(void *tcp_client, const char *addr, int port,
    void (*on_connect)(void *ctx, int ecode))
{
    struct connect_ctx *connect_ctx = new_connect_ctx(tcp_client, addr, port, on_connect);
    struct tcp_client_ctx *tcp_client_ctx = tcp_client;

    if (!connect_ctx) {
        cio_perror(CIO_ALLOC_ERROR, "cio_tcp_client_async_connect");
        on_connect(tcp_client_ctx->user_ctx, CIO_ALLOC_ERROR);
    }

    cio_event_loop_add_timer(tcp_client_ctx->event_loop, 0, connect_ctx, async_connect_impl);
}

struct write_ctx {
    struct tcp_client_ctx *tcp_client;
    void (*on_write)(void *ctx, int ecode, int written_len);
    const void *data;
    int len;
    int written;
};

static void write_ctx_on_destroy(void *ctx)
{
    struct write_ctx *write_ctx = ctx;
    write_ctx->tcp_client = NULL;
}

static void free_write_ctx(struct write_ctx *write_ctx)
{
    tcp_client_remove_wrapper_by_posted_ctx(write_ctx->tcp_client, write_ctx);
    free(write_ctx);
}

struct write_ctx *new_write_ctx(struct tcp_client_ctx *tcp_client,
    void (*on_write)(void *, int, int), const void *data, int len)
{
    struct write_ctx *write_ctx;

    write_ctx = malloc(sizeof(*write_ctx));
    if (!write_ctx)
        return NULL;

    write_ctx->tcp_client = tcp_client;
    write_ctx->on_write = on_write;
    write_ctx->data = data;
    write_ctx->len = len;
    write_ctx->written = 0;

    if (!new_wrapper_ctx(&tcp_client->wrapper_ctx, write_ctx, write_ctx_on_destroy)) {
        free_write_ctx(write_ctx);
        return NULL;
    }

    return write_ctx;
}

static void do_write(struct write_ctx *wctx)
{
    
}

static void async_write_impl(void *ctx)
{
    struct write_ctx *write_ctx = ctx;
    struct tcp_client_ctx *tcp_client_ctx = write_ctx->tcp_client;

    switch (tcp_client_ctx->cstate) {
    case CIO_CS_CONNECTED:
        break;
    case CIO_CS_CONNECTING:
    case CIO_CS_INITIAL:
    case CIO_CS_ERROR:
        free_write_ctx(write_ctx);
        fprintf(stdout, "async_write_impl: wrong state\n");
        return;
    default:
        assert(0);
    }

    do_write(write_ctx);
}

void cio_tcp_client_async_write(void *tcp_client, const void *data, int len,
    void (*on_write)(void *ctx, int ecode, int written_len))
{
    struct write_ctx *write_ctx;
    struct tcp_client_ctx *tcp_client_ctx = tcp_client;
    int ecode = 0;

    write_ctx = malloc(sizeof(*write_ctx));
    if (!write_ctx) {
        ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    write_ctx->tcp_client = tcp_client;
    write_ctx->on_write = on_write;
    write_ctx->data = data;
    write_ctx->len = len;
    write_ctx->written = 0;

    cio_event_loop_add_timer(tcp_client_ctx->event_loop, 0, write_ctx, async_write_impl);

fail:
    cio_perror(ecode, "cio_tcp_client_async_connect");
    on_write(tcp_client_ctx->user_ctx, ecode, 0);
}
