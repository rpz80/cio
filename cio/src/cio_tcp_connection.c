#include "cio_tcp_connection.h"
#include "cio_event_loop.h"
#include "cio_resolver.h"
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
    CIO_CS_ERROR,
    CIO_CS_DESTROYED
};

struct tcp_connection_ctx {
    enum obj_type type;
    void *event_loop;
    void *user_ctx;
    void *write_ctx;
    void *read_ctx;
    void *connect_ctx;
    int fd;
    int reference_count;
    enum connection_state cstate;
};

struct connect_ctx {
    struct tcp_connection_ctx *tcp_connection;
    void (*on_connect)(void *ctx, int ecode);
    void *resolver;
};

struct write_ctx {
    struct tcp_connection_ctx *tcp_connection;
    void (*on_write)(void *ctx, int ecode);
    const void *data;
    int len;
    int written;
};

struct read_ctx {
    struct tcp_connection_ctx *tcp_connection;
    void (*on_read)(void *ctx, int ecode, int read_bytes);
    void *data;
    int len;
    int read;
};

static void event_loop_cb(void *ctx, int fd, int flags);

static void *new_tcp_connection_impl(void *event_loop, void *ctx, int fd)
{
    struct tcp_connection_ctx *tctx;
    int cio_ecode = 0;

    tctx = malloc(sizeof(*tctx));
    if (!tctx)
        goto fail;

    tctx->type = CONNECTION;
    tctx->event_loop = event_loop;
    tctx->user_ctx = ctx;
    tctx->write_ctx = NULL;
    tctx->read_ctx = NULL;
    tctx->connect_ctx = NULL;
    tctx->reference_count = 1;
    
    if (fd == -1) {
        tctx->fd = -1;
        tctx->cstate = CIO_CS_INITIAL;
    } else {
        tctx->fd = fd;
        tctx->cstate = CIO_CS_CONNECTED;
        if ((cio_ecode = cio_event_loop_add_fd(tctx->event_loop, fd, CIO_FLAG_OUT | CIO_FLAG_IN,
                                               tctx, event_loop_cb))) {
            goto fail;
        }
    }

    return tctx;

fail:
    if (cio_ecode != CIO_NO_ERROR)
        cio_perror(cio_ecode, "cio_new_tcp_connection");
    else
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
    struct free_connection_ctx *free_connection_ctx = ctx;
    struct tcp_connection_ctx* connection_ctx = NULL;
    struct completion_ctx *completion_ctx = NULL;
    enum obj_type type;
    
    type = *((enum obj_type *) (free_connection_ctx->wrapped_ctx));
    switch (type) {
        case CONNECTION:
            connection_ctx = free_connection_ctx->wrapped_ctx;
            break;
        case COMPLETION:
            completion_ctx = free_connection_ctx->wrapped_ctx;
            connection_ctx = completion_ctx->wrapped_ctx;
            break;
        default:
            assert(0);
            return;
    }
    
    if (free_connection_ctx->do_destroy) {
        cio_event_loop_remove_fd(connection_ctx->event_loop, connection_ctx->fd);
        close(connection_ctx->fd);
    }

    if (--connection_ctx->reference_count == 0)
        free(connection_ctx);
    
    if (type == COMPLETION) {
        pthread_mutex_lock(&completion_ctx->mutex);
        pthread_cond_signal(&completion_ctx->cond);
        pthread_mutex_unlock(&completion_ctx->mutex);
    }
    
    free(free_connection_ctx);
}

void cio_free_tcp_connection_async(void *tcp_connection)
{
    struct tcp_connection_ctx *connection_ctx = tcp_connection;
    struct free_connection_ctx *free_connection_ctx;
    
    if (!connection_ctx)
        return;
    
    if (!(free_connection_ctx = malloc(sizeof(*free_connection_ctx)))) {
        perror("cio_free_tcp_connection_async");
        return;
    }
    
    free_connection_ctx->do_destroy = 1;
    free_connection_ctx->wrapped_ctx = connection_ctx;
    cio_event_loop_post(connection_ctx->event_loop, 0, free_connection_ctx,
                        free_tcp_connection_impl);
}

void cio_free_tcp_connection_sync(void *tcp_connection)
{
    struct tcp_connection_ctx *connection_ctx = tcp_connection;
    struct completion_ctx *completion_ctx;
    int ecode;
    
    if (!connection_ctx)
        return;

    if (!(completion_ctx = new_completion_ctx(connection_ctx)))
        goto fail;

    if ((ecode = completion_ctx_post_and_wait(completion_ctx, connection_ctx->event_loop,
                                              free_tcp_connection_impl, 1))) {
        errno = ecode;
        goto fail;
    }
    
    goto finally;

fail:
    perror("cio_free_tcp_connection_sync");
    
finally:
    free_completion_ctx(completion_ctx);
}

static void connect_ctx_try_next(struct connect_ctx *tctx);
static void free_connect_ctx(struct connect_ctx *cctx);
static void write_ctx_cleanup(struct write_ctx *write_ctx, int cio_error);
static void do_write(struct write_ctx *write_ctx);
static void do_read(struct read_ctx *read_ctx);

static void connect_ctx_cleanup(struct connect_ctx *connect_ctx, int cio_error)
{
    struct tcp_connection_ctx *tcp_connection_ctx = connect_ctx->tcp_connection;
    struct free_connection_ctx *free_connection_ctx;

    if (cio_error == CIO_NO_ERROR)
        tcp_connection_ctx->cstate = CIO_CS_CONNECTED;
    else if (cio_error != CIO_ALREADY_DESTROYED_ERROR)
        tcp_connection_ctx->cstate = CIO_CS_ERROR;

    free_connection_ctx = malloc(sizeof(*free_connection_ctx));
    assert(free_connection_ctx);
    free_connection_ctx->do_destroy = 0;
    free_connection_ctx->wrapped_ctx = tcp_connection_ctx;
    free_tcp_connection_impl(free_connection_ctx);
    
    connect_ctx->on_connect(tcp_connection_ctx->user_ctx, cio_error);
    free_connect_ctx(connect_ctx);
    tcp_connection_ctx->connect_ctx = NULL;
}

static void write_ctx_cleanup(struct write_ctx *write_ctx, int cio_error)
{
    struct tcp_connection_ctx *tcp_connection_ctx = write_ctx->tcp_connection;
    struct free_connection_ctx *free_connection_ctx;
    
    if (cio_error != CIO_ALREADY_DESTROYED_ERROR && cio_error != CIO_NO_ERROR)
        tcp_connection_ctx->cstate = CIO_CS_ERROR;
    
    free_connection_ctx = malloc(sizeof(*free_connection_ctx));
    assert(free_connection_ctx);
    free_connection_ctx->do_destroy = 0;
    free_connection_ctx->wrapped_ctx = tcp_connection_ctx;
    free_tcp_connection_impl(free_connection_ctx);
    
    write_ctx->on_write(tcp_connection_ctx->user_ctx, cio_error);
    free(write_ctx);
    tcp_connection_ctx->write_ctx = NULL;
}

static void read_ctx_cleanup(struct read_ctx *read_ctx, int cio_error)
{
    struct tcp_connection_ctx *tcp_connection_ctx = read_ctx->tcp_connection;
    struct free_connection_ctx *free_connection_ctx;
    
    if (cio_error != CIO_ALREADY_DESTROYED_ERROR && cio_error != CIO_NO_ERROR)
        tcp_connection_ctx->cstate = CIO_CS_ERROR;
  
    free_connection_ctx = malloc(sizeof(*free_connection_ctx));
    assert(free_connection_ctx);
    free_connection_ctx->do_destroy = 0;
    free_connection_ctx->wrapped_ctx = tcp_connection_ctx;
    free_tcp_connection_impl(free_connection_ctx);

    read_ctx->on_read(tcp_connection_ctx->user_ctx, cio_error, read_ctx->read);
    free(read_ctx);
    tcp_connection_ctx->read_ctx = NULL;
}

static void clean_all_contexts(struct tcp_connection_ctx *tcp_connection_ctx,
                               enum CIO_ERROR cio_error)
{
    if (tcp_connection_ctx->connect_ctx)
        connect_ctx_cleanup(tcp_connection_ctx->connect_ctx, cio_error);
    if (tcp_connection_ctx->write_ctx)
        write_ctx_cleanup(tcp_connection_ctx->write_ctx, cio_error);
    if (tcp_connection_ctx->connect_ctx)
        read_ctx_cleanup(tcp_connection_ctx->read_ctx, cio_error);
}

static void event_loop_cb(void *ctx, int fd, int flags)
{
    struct tcp_connection_ctx *tcp_connection_ctx = ctx;

    assert(tcp_connection_ctx);
    switch (tcp_connection_ctx->cstate) {
        case CIO_CS_CONNECTING:
            assert(tcp_connection_ctx->connect_ctx);
            assert(!tcp_connection_ctx->write_ctx);
            assert(!tcp_connection_ctx->read_ctx);
            if (flags & CIO_FLAG_OUT)
                return connect_ctx_cleanup(tcp_connection_ctx->connect_ctx, CIO_NO_ERROR);
            fprintf(stdout, "on_connect_cb, error flags: %d\n", flags);
            connect_ctx_try_next(tcp_connection_ctx->connect_ctx);
            break;
        case CIO_CS_CONNECTED:
            assert(!tcp_connection_ctx->connect_ctx);
            if ((flags & CIO_FLAG_OUT) && tcp_connection_ctx->write_ctx)
                do_write(tcp_connection_ctx->write_ctx);
            if (tcp_connection_ctx->cstate == CIO_CS_DESTROYED)
                return;
            if ((flags & CIO_FLAG_IN) && tcp_connection_ctx->read_ctx)
                do_read(tcp_connection_ctx->read_ctx);
            break;
        case CIO_CS_DESTROYED:
            clean_all_contexts(tcp_connection_ctx, CIO_ALREADY_DESTROYED_ERROR);
            break;
        default:
            assert(0);
            clean_all_contexts(tcp_connection_ctx, CIO_WRONG_STATE_ERROR);
            break;
    }
}

static void free_connect_ctx(struct connect_ctx *connect_ctx)
{
    cio_free_resolver(connect_ctx->resolver);
    free(connect_ctx);
}

static struct connect_ctx *new_connect_ctx(struct tcp_connection_ctx *tcp_connection,
    const char *addr, int port, void (*on_connect)(void *ctx, int ecode))
{
    struct connect_ctx *connect_ctx;

    connect_ctx = malloc(sizeof(*connect_ctx));
    if (!connect_ctx)
        return NULL;

    connect_ctx->tcp_connection = tcp_connection;
    connect_ctx->on_connect = on_connect;
    connect_ctx->resolver = cio_new_resolver(addr, port, AF_UNSPEC, SOCK_STREAM, CIO_CLIENT);
    if (!connect_ctx->resolver) {
        free(connect_ctx);
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
    int set;

    assert(tcp_connection_ctx->cstate == CIO_CS_CONNECTING);
    if ((cio_ecode = cio_resolver_next_endpoint(connect_ctx->resolver, &ainfo)))
        goto fail;

    if (tcp_connection_ctx->fd != -1) {
        close(tcp_connection_ctx->fd);
        cio_event_loop_remove_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd);
    }
    
    if ((tcp_connection_ctx->fd = socket(ainfo.ai_family, ainfo.ai_socktype, 0)) == -1) {
        system_ecode = errno;
        goto fail;
    }

    tcp_connection_ctx->connect_ctx = connect_ctx;
    if ((cio_ecode = cio_event_loop_add_fd(tcp_connection_ctx->event_loop, tcp_connection_ctx->fd,
                                           CIO_FLAG_OUT | CIO_FLAG_IN, tcp_connection_ctx,
                                           event_loop_cb))) {
        goto fail;
    }

#ifdef __APPLE__
    set = 1;
    setsockopt(tcp_connection_ctx->fd, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
#endif /* __APPLE__ */

    (void) set;
    if ((system_ecode = toggle_fd_nonblocking(tcp_connection_ctx->fd, 1)))
        goto fail;

    if (connect(tcp_connection_ctx->fd, ainfo.ai_addr, ainfo.ai_addrlen))
        system_ecode = errno;

    switch (system_ecode) {
    case 0:
        connect_ctx_cleanup(connect_ctx, CIO_NO_ERROR);
        break;
    case EINPROGRESS:
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

    connect_ctx_cleanup(connect_ctx, CIO_UNKNOWN_ERROR);
}

static void async_connect_impl(void *ctx)
{
    struct connect_ctx *connect_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = connect_ctx->tcp_connection;

    ++tcp_connection_ctx->reference_count;
    switch (tcp_connection_ctx->cstate) {
        case CIO_CS_CONNECTED:
        case CIO_CS_CONNECTING:
        case CIO_CS_IN_PROGRESS:
            connect_ctx_cleanup(connect_ctx, CIO_WRONG_STATE_ERROR);
            return;
        case CIO_CS_DESTROYED:
            connect_ctx_cleanup(connect_ctx, CIO_ALREADY_DESTROYED_ERROR);
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

    return write_ctx;
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
            if (write_ctx->written == write_ctx->len)
                return write_ctx_cleanup(write_ctx, CIO_NO_ERROR);
            else
                continue;
        } else {
            system_ecode = errno;
            if (system_ecode == EWOULDBLOCK)
                return;
            goto fail;
        }
    }

fail:
    if (cio_ecode)
        cio_perror(cio_ecode, "do_write");
    if (system_ecode)
        perror("do_write");

    write_ctx_cleanup(write_ctx, CIO_WRITE_ERROR);
}

static void async_write_impl(void *ctx)
{
    struct write_ctx *write_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = write_ctx->tcp_connection;

    assert(tcp_connection_ctx);
    tcp_connection_ctx->reference_count++;
    switch (tcp_connection_ctx->cstate) {
        case CIO_CS_DESTROYED:
            return write_ctx_cleanup(write_ctx, CIO_ALREADY_DESTROYED_ERROR);
        case CIO_CS_CONNECTED:
            assert(!tcp_connection_ctx->write_ctx);
            if (tcp_connection_ctx->write_ctx) {
                write_ctx_cleanup(write_ctx, CIO_ALREADY_EXISTS_ERROR);
                return;
            }
            tcp_connection_ctx->write_ctx = write_ctx;
            do_write(write_ctx);
            break;
        default:
            return write_ctx_cleanup(write_ctx, CIO_WRONG_STATE_ERROR);
    }
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

    return read_ctx;
}

static void do_read(struct read_ctx *read_ctx)
{
    int cio_ecode = CIO_NO_ERROR;
    int system_ecode = 0;
    struct tcp_connection_ctx *tcp_connection_ctx = read_ctx->tcp_connection;

    read_ctx->read = read(tcp_connection_ctx->fd, read_ctx->data, read_ctx->len - read_ctx->read);
    if (read_ctx->read >= 0) {
        return read_ctx_cleanup(read_ctx, CIO_NO_ERROR);
    } else {
        read_ctx->read = 0;
        system_ecode = errno;
        if (system_ecode == EWOULDBLOCK)
            return;
    }

    if (cio_ecode)
        cio_perror(cio_ecode, "do_read");
    if (system_ecode)
        perror("do_read");

    read_ctx_cleanup(read_ctx, CIO_READ_ERROR);
}

static void async_read_impl(void *ctx)
{
    struct read_ctx *read_ctx = ctx;
    struct tcp_connection_ctx *tcp_connection_ctx = read_ctx->tcp_connection;
    
    assert(tcp_connection_ctx);
    tcp_connection_ctx->reference_count++;
    switch (tcp_connection_ctx->cstate) {
        case CIO_CS_DESTROYED:
            return read_ctx_cleanup(read_ctx, CIO_ALREADY_DESTROYED_ERROR);
        case CIO_CS_CONNECTED:
            assert(!tcp_connection_ctx->read_ctx);
            if (tcp_connection_ctx->read_ctx) {
                read_ctx_cleanup(read_ctx, CIO_ALREADY_EXISTS_ERROR);
                return;
            }
            tcp_connection_ctx->read_ctx = read_ctx;
            do_read(read_ctx);
            break;
        default:
            return read_ctx_cleanup(read_ctx, CIO_WRONG_STATE_ERROR);
    }
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
