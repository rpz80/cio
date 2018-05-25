#include "event_loop.h"
#include "pollset.h"
#include "hash_set.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>

struct user_fd_cb_ctx {
    pollset_cb_t cb;
    void *ctx;
    int fd;
};

struct timer_cb_ctx {
    void (*action)(void *);
    void *action_ctx;
    int due_time;
    struct timer_cb_ctx *next;
};

struct event_loop {
    void *pollset;
    void *fd_set;
    int need_stop;
    int event_pipe[2];
    struct timer_cb_ctx* timer_actions;
    pthread_mutex_t mutex;
};

enum event_code {
    STOP,
    WAKE_UP
};

static void event_pipe_cb(void *ctx, int fd, int flags)
{
    struct event_loop *el = (struct event_loop *) ctx;

    assert(fd == el->event_pipe[0]);
    if (fd != el->event_pipe[0]) {
        cio_perror(CIO_UNKNOWN_ERROR, "Wrong event pipe descriptor");
        return;
    }

    assert(flags & CIO_FLAG_IN);
    if (!(flags & CIO_FLAG_IN))
        cio_perror(CIO_UNKNOWN_ERROR, "Wrong event pipe flags");

    el->need_stop = 1;
}

static int user_fd_cb_ctx_cmp(const void *l, const void *r)
{
    return ((struct user_fd_cb_ctx *) l)->fd == ((struct user_fd_cb_ctx *) r)->fd;
}

static void user_fd_cb_ctx_hash_data(const void *elem, void **data, int *len)
{
    struct user_fd_cb_ctx *ctx = (struct user_fd_cb_ctx *)elem;
    *data = &ctx->fd;
    *len = sizeof(ctx->fd);
}

static void user_fd_cb_ctx_release(void *elem)
{
    free(elem);
}

void *cio_new_event_loop(int expected_capacity)
{
    struct event_loop *el = malloc(sizeof(struct event_loop));
    int ecode = 0;

    el->pollset = cio_new_pollset();
    el->need_stop = 0;
    el->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    el->timer_actions = NULL;

    el->fd_set = cio_new_hash_set(
        expected_capacity / 0.75,
        user_fd_cb_ctx_cmp,
        user_fd_cb_ctx_hash_data,
        user_fd_cb_ctx_release);

    if (!el->fd_set) {
        ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    if (pipe(el->event_pipe))
        goto fail;

    if ((ecode = cio_pollset_add(el->pollset, el->event_pipe[1], CIO_FLAG_IN)))
        goto fail;

    return el;

fail:
    if (ecode)
        cio_perror(ecode, NULL);
    else
        perror("cio_new_event_loop");

    cio_free_event_loop(el);
    return NULL;
}

void cio_free_event_loop(void *loop)
{
    struct event_loop *el = (struct event_loop *) loop;
    cio_free_pollset(el->pollset);
    free(loop);
}

static void pollset_cb(void *ctx, int fd, int flags)
{

}

int cio_event_loop_run(void *loop)
{
    struct event_loop *el = (struct event_loop *) loop;
    do {
        cio_pollset_poll(el->pollset, -1, el, pollset_cb);
    } while (!el->need_stop);

    return CIO_NO_ERROR;
}

static int send_pipe_event(void *loop, int code)
{
    struct event_loop *el = (struct event_loop *) loop;

    return write(el->event_pipe[1], &code, sizeof(code)) > 0 ? CIO_NO_ERROR : CIO_WRITE_ERROR;
}

int cio_event_loop_stop(void *loop)
{
    return send_pipe_event(loop, STOP);
}

struct add_ctx {
    void *loop;
    int fd;
    int flags;
    void *cb_ctx;
    pollset_cb_t cb;
};

static void add_fd_impl(void *ctx)
{
    struct add_ctx *actx = (struct add_ctx *) ctx;
    struct event_loop *el = (struct event_loop *) actx->loop;
    struct user_fd_cb_ctx *fd_ctx = malloc(sizeof(struct user_fd_cb_ctx));
    int ecode = 0;

    if (!fd_ctx) {
        ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    fd_ctx->fd = actx->fd;
    fd_ctx->ctx = actx->cb_ctx;
    fd_ctx->cb = actx->cb;

    if ((ecode = cio_pollset_add(el->pollset, actx->fd, actx->flags))) {
        goto fail;
    }

    if (!cio_hash_set_add(el->fd_set, fd_ctx)) {
        ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    free(actx);
    return;

fail:
     if (ecode)
        cio_perror(ecode, NULL);

    free(actx);
    free(fd_ctx);
}

int cio_event_loop_add_fd(void *loop, int fd, int flags, void *cb_ctx, pollset_cb_t cb)
{
    struct add_ctx *actx = malloc(sizeof(struct add_ctx));

    if (!actx)
        return CIO_ALLOC_ERROR;

    actx->cb = cb;
    actx->cb_ctx = cb_ctx;
    actx->fd = fd;
    actx->flags = flags;
    actx->loop = loop;

    cio_event_loop_add_timer(loop, 0, actx, add_fd_impl);

    return CIO_NO_ERROR;
}

int cio_event_loop_remove_fd(void *loop, int fd)
{

}

int cio_event_loop_add_timer(void *loop, int timeout_ms, void *cb_ctx, void (*cb)(void *))
{
    struct event_loop *el = (struct event_loop *) loop;
    int ecode = 0;
    struct timer_cb_ctx *ta, *prev_ta;
    struct timer_cb_ctx *timer_ctx;
    struct timeval tv;

    if (pthread_mutex_lock(&el->mutex)) {
        ecode = CIO_UNKNOWN_ERROR;
        goto fail;
    }

    if (gettimeofday(&tv, NULL)) {
        ecode = CIO_UNKNOWN_ERROR;
        goto fail;
    }

    if (!(timer_ctx = malloc(sizeof(*timer_ctx)))) {
        ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    timer_ctx->action = cb;
    timer_ctx->action_ctx = cb_ctx;
    timer_ctx->due_time = timeMsFromTv(&tv) + timeout_ms;
    timer_ctx->next = NULL;

    ta = prev_ta = el->timer_actions;
    while (ta && ta->due_time < timer_ctx->due_time) {
        prev_ta = ta;
        ta = ta->next;
    }

    if (prev_ta) {
        prev_ta->next = timer_ctx;
        timer_ctx->next = ta;
    } else {
        el->timer_actions = timer_ctx;
    }

    if (pthread_mutex_unlock(&el->mutex)) {
        ecode = CIO_UNKNOWN_ERROR;
        goto fail;
    }

    return send_pipe_event(loop, WAKE_UP);

fail:
    pthread_mutex_unlock(&el->mutex);
    cio_perror(ecode, NULL);

    return ecode;
}
