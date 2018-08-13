#include "tcp_connection.h"
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
    CIO_CS_IN_PROGRESS,
    CIO_CS_ERROR
};

/* This struct is used to inform all currently alive contexts (which reside in the event_loop) that
 * main context is destroyed in order not to crash on callback execution after tcp_connection
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

struct tcp_connection_ctx {
    void *event_loop;
    void *user_ctx;
    int fd;
    enum connection_state cstate;
    struct wrapper_ctx *wrapper_ctx;
};

static void *new_tcp_connection_impl(void *event_loop, void *ctx, int fd)
{
    struct tcp_connection_ctx *tctx;

    tctx = malloc(sizeof(*tctx));
    if (!tctx)
        goto fail;

    tctx->event_loop = event_loop;
    tctx->user_ctx = ctx;
    tctx->wrapper_ctx = NULL;

    if (fd == -1) {
        tctx->fd = -1;
        tctx->cstate = CIO_CS_INITIAL;
    } else {
        tctx->fd = fd;
        tctx->cstate = CIO_CS_CONNECTED;
    }

    return tctx;

fail:
    perror("cio_new_tcp_connection");
    free(tctx);
    return NULL;
}

void *cio_new_tcp_connection(void *event_loop, void *ctx)
{
    return new_tcp_connection_impl(event_loop, ctx, -1);
}

void *cio_new_tcp_connection_connected_fd(void *event_loop, void *ctx, int fd)
{
    return new_tcp_connection_impl(event_loop, ctx, fd);
}

static void free_tcp_connection_impl(void *ctx)
{
    struct tcp_connection_ctx *tcp_connection_ctx = ctx;

    close(tcp_connection_ctx->fd);
    cio_event_loop_remove_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd);
    free_all_wrappers(tcp_connection_ctx->wrapper_ctx);
    free(tcp_connection_ctx);
}

void cio_free_tcp_connection(void *tcp_connection)
{
    struct tcp_connection_ctx *tctx = tcp_connection;
    cio_event_loop_post(tctx->event_loop, 0, tctx, free_tcp_connection_impl);
}

static void tcp_connection_remove_wrapper_by_posted_ctx(struct tcp_connection_ctx *tcp_connection_ctx,
    void *posted_ctx)
{
    struct wrapper_ctx *wrapper_ctx, *tmp_wrapper_ctx;

    if (!tcp_connection_ctx)
        return;

    wrapper_ctx = tcp_connection_ctx->wrapper_ctx;
    tmp_wrapper_ctx = NULL;

    while (wrapper_ctx) {
        if (wrapper_ctx->posted_ctx == posted_ctx) {
            if (tmp_wrapper_ctx)
                tmp_wrapper_ctx->next = wrapper_ctx->next;
            else
                tcp_connection_ctx->wrapper_ctx = wrapper_ctx->next;
            free(wrapper_ctx);
            break;
        } else {
            tmp_wrapper_ctx = wrapper_ctx;
            wrapper_ctx = wrapper_ctx->next;
        }
    }
}

struct connect_ctx {
    struct tcp_connection_ctx *tcp_connection;
    void (*on_connect)(void *ctx, int ecode);
    void *resolver;
};

static void connect_ctx_try_next(struct connect_ctx *tctx);
static void free_connect_ctx(struct connect_ctx *cctx);

static void connect_ctx_cleanup(struct connect_ctx *connect_ctx, int failed, int cio_error)
{
    struct tcp_connection_ctx *tcp_connection_ctx = connect_ctx->tcp_connection;

    cio_event_loop_remove_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd);
    if (failed) {
        close(tcp_connection_ctx->fd);
        tcp_connection_ctx->cstate = CIO_CS_ERROR;
    } else {
        tcp_connection_ctx->cstate = CIO_CS_CONNECTED;
    }
    connect_ctx->on_connect(tcp_connection_ctx->user_ctx, cio_error);
    free_connect_ctx(connect_ctx);
}

static void connect_ctx_event_loop_cb(void *ctx, int fd, int flags)
{
    struct connect_ctx *connect_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = connect_ctx->tcp_connection;

    if (!tcp_connection_ctx) {
        free_connect_ctx(connect_ctx);
        return;
    }

    assert(fd == tcp_connection_ctx->fd);
    if (flags & CIO_FLAG_OUT)
        return connect_ctx_cleanup(connect_ctx, 0, CIO_NO_ERROR);

    fprintf(stdout, "on_connect_cb, error flags: %d\n", flags);
    connect_ctx_try_next(connect_ctx);
}

static void free_connect_ctx(struct connect_ctx *connect_ctx)
{
    struct tcp_connection_ctx *tcp_connection_ctx = connect_ctx->tcp_connection;

    tcp_connection_remove_wrapper_by_posted_ctx(tcp_connection_ctx, connect_ctx);
    cio_free_resolver(connect_ctx->resolver);
    free(connect_ctx);
}

static void connect_ctx_on_destroy(void *ctx)
{
    struct connect_ctx *connect_ctx = ctx;
    connect_ctx->tcp_connection = NULL;
}

static struct connect_ctx *new_connect_ctx(struct tcp_connection_ctx *tcp_connection, const char *addr,
    int port, void (*on_connect)(void *ctx, int ecode))
{
    struct connect_ctx *connect_ctx;

    connect_ctx = malloc(sizeof(*connect_ctx));
    if (!connect_ctx)
        return NULL;

    connect_ctx->tcp_connection = tcp_connection;
    connect_ctx->on_connect = on_connect;
    connect_ctx->resolver = cio_new_resolver(addr, port, AF_UNSPEC, SOCK_STREAM,
        CIO_CLIENT);
    if (!connect_ctx->resolver) {
        free(connect_ctx);
        return NULL;
    }

    if (!new_wrapper_ctx(&tcp_connection->wrapper_ctx, connect_ctx, connect_ctx_on_destroy)) {
        free_connect_ctx(connect_ctx);
        return NULL;
    }

    return connect_ctx;
}

static void connect_ctx_try_next(struct connect_ctx *connect_ctx)
{
    int cio_ecode = CIO_NO_ERROR;
    int system_ecode = 0;
    struct tcp_connection_ctx *tcp_connection_ctx = connect_ctx->tcp_connection;
    struct addrinfo ainfo;

    assert(tcp_connection_ctx->cstate == CIO_CS_CONNECTING);
    if ((cio_ecode = cio_resolver_next_endpoint(connect_ctx->resolver, &ainfo)))
        goto fail;

    close(tcp_connection_ctx->fd);
    if ((tcp_connection_ctx->fd = socket(ainfo.ai_family, ainfo.ai_socktype, 0)) == -1) {
        system_ecode = errno;
        goto fail;
    }

#ifdef __APPLE__
    int set = 1;
    setsockopt(tcp_connection_ctx->fd, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
#endif /* __APPLE__ */

    if ((system_ecode = toggle_fd_nonblocking(tcp_connection_ctx->fd, 1)))
        goto fail;

    if (connect(tcp_connection_ctx->fd, ainfo.ai_addr, ainfo.ai_addrlen))
        system_ecode = errno;

    switch (system_ecode) {
    case 0:
        connect_ctx_cleanup(connect_ctx, 0, CIO_NO_ERROR);
        break;
    case EINPROGRESS:
        cio_event_loop_remove_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd);
        cio_ecode = cio_event_loop_add_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd, CIO_FLAG_OUT,
            connect_ctx, connect_ctx_event_loop_cb);
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

    connect_ctx_cleanup(connect_ctx, 1, CIO_UNKNOWN_ERROR);
}

static void async_connect_impl(void *ctx)
{
    struct connect_ctx *connect_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = connect_ctx->tcp_connection;

    if (!tcp_connection_ctx) {
        free_connect_ctx(connect_ctx);
        return;
    }

    switch (tcp_connection_ctx->cstate) {
    case CIO_CS_CONNECTED:
    case CIO_CS_CONNECTING:
    case CIO_CS_IN_PROGRESS:
        connect_ctx->on_connect(tcp_connection_ctx->user_ctx, CIO_WRONG_STATE_ERROR);
        free_connect_ctx(connect_ctx);
        return;
    case CIO_CS_INITIAL:
    case CIO_CS_ERROR:
        tcp_connection_ctx->cstate = CIO_CS_CONNECTING;
        break;
    }

    connect_ctx_try_next(connect_ctx);
}

void cio_tcp_connection_async_connect(void *tcp_connection, const char *addr, int port,
    void (*on_connect)(void *ctx, int ecode))
{
    struct connect_ctx *connect_ctx = new_connect_ctx(tcp_connection, addr, port, on_connect);
    struct tcp_connection_ctx *tcp_connection_ctx = tcp_connection;

    if (!connect_ctx) {
        cio_perror(CIO_ALLOC_ERROR, "cio_tcp_connection_async_connect");
        on_connect(tcp_connection_ctx->user_ctx, CIO_ALLOC_ERROR);
    }

    cio_event_loop_post(tcp_connection_ctx->event_loop, 0, connect_ctx, async_connect_impl);
}

struct write_ctx {
    struct tcp_connection_ctx *tcp_connection;
    void (*on_write)(void *ctx, int ecode);
    const void *data;
    int len;
    int written;
};

static void write_ctx_on_destroy(void *ctx)
{
    struct write_ctx *write_ctx = ctx;
    write_ctx->tcp_connection = NULL;
}

static void free_write_ctx(struct write_ctx *write_ctx)
{
    tcp_connection_remove_wrapper_by_posted_ctx(write_ctx->tcp_connection, write_ctx);
    free(write_ctx);
}

struct write_ctx *new_write_ctx(struct tcp_connection_ctx *tcp_connection,
    void (*on_write)(void *, int), const void *data, int len)
{
    struct write_ctx *write_ctx;

    write_ctx = malloc(sizeof(*write_ctx));
    if (!write_ctx)
        return NULL;

    write_ctx->tcp_connection = tcp_connection;
    write_ctx->on_write = on_write;
    write_ctx->data = data;
    write_ctx->len = len;
    write_ctx->written = 0;

    if (!new_wrapper_ctx(&tcp_connection->wrapper_ctx, write_ctx, write_ctx_on_destroy)) {
        free_write_ctx(write_ctx);
        return NULL;
    }

    return write_ctx;
}

static void do_write(struct write_ctx *write_ctx);

static void write_ctx_cleanup(struct write_ctx *write_ctx, int failed, int cio_error)
{
    struct tcp_connection_ctx *tcp_connection_ctx = write_ctx->tcp_connection;

    cio_event_loop_remove_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd);
    if (failed) {
        close(tcp_connection_ctx->fd);
        tcp_connection_ctx->cstate = CIO_CS_ERROR;
    } else {
        tcp_connection_ctx->cstate = CIO_CS_CONNECTED;
    }
    write_ctx->on_write(tcp_connection_ctx->user_ctx, cio_error);
    free_write_ctx(write_ctx);
}

static void write_ctx_event_loop_cb(void *ctx, int fd, int flags)
{
    struct write_ctx *write_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = write_ctx->tcp_connection;

    if (!tcp_connection_ctx) {
        free_write_ctx(write_ctx);
        return;
    }

    assert(fd == tcp_connection_ctx->fd);
    if (flags & CIO_FLAG_OUT)
        return do_write(write_ctx);

    fprintf(stdout, "on_write_cb, error flags: %d\n", flags);
    write_ctx_cleanup(write_ctx, 1, CIO_POLL_ERROR);
}

static void do_write(struct write_ctx *write_ctx)
{
    int cio_ecode = CIO_NO_ERROR;
    int system_ecode = 0;
    int write_result = 0;
    struct tcp_connection_ctx *tcp_connection_ctx = write_ctx->tcp_connection;

    while (write_ctx->written != write_ctx->len) {
#ifdef __APPLE__
        write_result = write(tcp_connection_ctx->fd, write_ctx->data,
            write_ctx->len - write_ctx->written);
#else
        write_result = send(tcp_connection_ctx->fd, write_ctx->data,
            write_ctx->len - write_ctx->written, MSG_NOSIGNAL);
#endif
        if (write_result >= 0) {
            write_ctx->written += write_result;
            if (write_ctx->written == write_ctx->len) {
                write_ctx_cleanup(write_ctx, 0, CIO_NO_ERROR);
                return;
            } else {
                continue;
            }
        } else {
            system_ecode = errno;
            switch (system_ecode) {
            case EWOULDBLOCK:
//                if (do_remove_add) {
//                    cio_event_loop_remove_fd(tcp_connection_ctx->event_loop,
//                        tcp_connection_ctx->fd);
//                    cio_ecode = cio_event_loop_add_fd(tcp_connection_ctx->event_loop,
//                        tcp_connection_ctx->fd, CIO_FLAG_OUT, write_ctx, write_ctx_event_loop_cb);
//                    if (cio_ecode)
//                        goto fail;
//                }
                return;
            case 0:
                assert(0);
                break;
            default:
                goto fail;
            }
        }
    }

fail:
    if (cio_ecode)
        cio_perror(cio_ecode, "do_write");
    if (system_ecode)
        perror("do_write");

    write_ctx_cleanup(write_ctx, 1, CIO_WRITE_ERROR);
}

static void async_write_impl(void *ctx)
{
    struct write_ctx *write_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = write_ctx->tcp_connection;

    if (!tcp_connection_ctx) {
        free_write_ctx(write_ctx);
        return;
    }

    switch (tcp_connection_ctx->cstate) {
    case CIO_CS_CONNECTED:
        tcp_connection_ctx->cstate = CIO_CS_IN_PROGRESS;
        break;
    case CIO_CS_CONNECTING:
    case CIO_CS_INITIAL:
    case CIO_CS_ERROR:
    case CIO_CS_IN_PROGRESS:
        write_ctx->on_write(tcp_connection_ctx->user_ctx, CIO_WRONG_STATE_ERROR);
        free_write_ctx(write_ctx);
        return;
    default:
        assert(0);
    }

    do_write(write_ctx);
}

void cio_tcp_connection_async_write(void *tcp_connection, const void *data, int len,
    void (*on_write)(void *ctx, int ecode))
{
    struct write_ctx *write_ctx = new_write_ctx(tcp_connection, on_write, data, len);
    struct tcp_connection_ctx *tcp_connection_ctx = tcp_connection;

    if (!write_ctx) {
        cio_perror(CIO_ALLOC_ERROR, "cio_tcp_connection_async_write");
        on_write(tcp_connection_ctx->user_ctx, CIO_ALLOC_ERROR);
        return;
    }

    cio_event_loop_post(tcp_connection_ctx->event_loop, 0, write_ctx, async_write_impl);
}

struct read_ctx {
    struct tcp_connection_ctx *tcp_connection;
    void (*on_read)(void *ctx, int ecode, int read_bytes);
    void *data;
    int len;
    int read;
};

static void read_ctx_on_destroy(void *ctx)
{
    struct read_ctx *read_ctx = ctx;
    read_ctx->tcp_connection = NULL;
}

static void free_read_ctx(struct read_ctx *read_ctx)
{
    tcp_connection_remove_wrapper_by_posted_ctx(read_ctx->tcp_connection, read_ctx);
    free(read_ctx);
}

struct read_ctx *new_read_ctx(struct tcp_connection_ctx *tcp_connection,
    void (*on_read)(void *, int, int), void *data, int len)
{
    struct read_ctx *read_ctx;

    read_ctx = malloc(sizeof(*read_ctx));
    if (!read_ctx)
        return NULL;

    read_ctx->tcp_connection = tcp_connection;
    read_ctx->on_read = on_read;
    read_ctx->data = data;
    read_ctx->len = len;
    read_ctx->read = 0;

    if (!new_wrapper_ctx(&tcp_connection->wrapper_ctx, read_ctx, read_ctx_on_destroy)) {
        free_read_ctx(read_ctx);
        return NULL;
    }

    return read_ctx;
}

static void do_read(struct read_ctx *read_ctx);

static void read_ctx_cleanup(struct read_ctx *read_ctx, int failed, int cio_error)
{
    struct tcp_connection_ctx *tcp_connection_ctx = read_ctx->tcp_connection;

    cio_event_loop_remove_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd);
    if (failed) {
        close(tcp_connection_ctx->fd);
        tcp_connection_ctx->cstate = CIO_CS_ERROR;
    } else {
        tcp_connection_ctx->cstate = CIO_CS_CONNECTED;
    }
    read_ctx->on_read(tcp_connection_ctx->user_ctx, cio_error, read_ctx->read);
    free_read_ctx(read_ctx);
}

static void read_ctx_event_loop_cb(void *ctx, int fd, int flags)
{
    struct read_ctx *read_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = read_ctx->tcp_connection;

    if (!tcp_connection_ctx) {
        free_read_ctx(read_ctx);
        return;
    }

    assert(fd == tcp_connection_ctx->fd);
    if (flags & CIO_FLAG_IN)
        return do_read(read_ctx);

    fprintf(stdout, "on_read_cb, error flags: %d\n", flags);
    read_ctx_cleanup(read_ctx, 1, CIO_POLL_ERROR);
}

static void do_read(struct read_ctx *read_ctx)
{
    int cio_ecode = CIO_NO_ERROR;
    int system_ecode = 0;
    int read_result = 0;
    struct tcp_connection_ctx *tcp_connection_ctx = read_ctx->tcp_connection;

    read_result = read(tcp_connection_ctx->fd, read_ctx->data, read_ctx->len - read_ctx->read);
    if (read_result > 0) {
        read_ctx->read = read_result;
        read_ctx_cleanup(read_ctx, 0, CIO_NO_ERROR);
        return;
    } else if (read_result == 0) {
        read_ctx_cleanup(read_ctx, 0, CIO_NO_ERROR);
        return;
    } else {
        system_ecode = errno;
        switch (system_ecode) {
        case EWOULDBLOCK:
//            if (do_remove_add) {
//                cio_event_loop_remove_fd(tcp_connection_ctx->event_loop,
//                    tcp_connection_ctx->fd);
//                cio_ecode = cio_event_loop_add_fd(tcp_connection_ctx->event_loop,
//                    tcp_connection_ctx->fd, CIO_FLAG_IN, read_ctx, read_ctx_event_loop_cb);
//                if (cio_ecode)
//                    goto fail;
//            }
            return;
        case 0:
            assert(0);
            break;
        default:
            goto fail;
        }
    }

fail:
    if (cio_ecode)
        cio_perror(cio_ecode, "do_read");
    if (system_ecode)
        perror("do_read");

    read_ctx_cleanup(read_ctx, 1, CIO_READ_ERROR);
}

static void async_read_impl(void *ctx)
{
    struct read_ctx *read_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = read_ctx->tcp_connection;

    if (!tcp_connection_ctx) {
        free_read_ctx(read_ctx);
        return;
    }

    switch (tcp_connection_ctx->cstate) {
    case CIO_CS_CONNECTED:
        tcp_connection_ctx->cstate = CIO_CS_IN_PROGRESS;
        break;
    case CIO_CS_CONNECTING:
    case CIO_CS_INITIAL:
    case CIO_CS_ERROR:
    case CIO_CS_IN_PROGRESS:
        read_ctx->on_read(tcp_connection_ctx, CIO_WRONG_STATE_ERROR, 0);
        free_read_ctx(read_ctx);
        return;
    }

    do_read(read_ctx);
}

void cio_tcp_connection_async_read(void *tcp_connection, void *data, int len,
    void (*on_read)(void *ctx, int ecode, int bytes_read))
{
    struct read_ctx *read_ctx = new_read_ctx(tcp_connection, on_read, data, len);
    struct tcp_connection_ctx *tcp_connection_ctx = tcp_connection;

    if (!read_ctx) {
        cio_perror(CIO_ALLOC_ERROR, "cio_tcp_connection_async_read");
        on_read(tcp_connection_ctx->user_ctx, CIO_ALLOC_ERROR, 0);
        return;
    }

    cio_event_loop_post(tcp_connection_ctx->event_loop, 0, read_ctx, async_read_impl);
}
