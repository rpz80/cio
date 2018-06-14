#include "event_loop.h"
#include "pollset.h"
#include "hash_set.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

struct user_fd_cb_ctx {
    pollset_cb_t cb;
    void *ctx;
    int fd;
};

struct timer_cb_ctx {
    void (*action)(void *);
    void *action_ctx;
    long long due_time;
    struct timer_cb_ctx *next;
};

struct event_loop {
    void *pollset;
    void *fd_set;
    int need_stop;
    int poll_timeout_ms;
    int event_pipe[2];
    struct timer_cb_ctx* timer_actions;
    pthread_mutex_t mutex;
    pthread_t self_id;
};

enum event_code {
    STOP,
    WAKE_UP
};

static int toggle_fd_blocking(int fd, int on)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL) == -1))
        goto fail;

    if (on)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    if ((fcntl(fd, F_SETFL, flags) == -1))
        goto fail;

    return CIO_NO_ERROR;

fail:
    perror("toggle_fd_blocking");
    return errno;
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
    struct event_loop *el = malloc(sizeof(struct event_loop));;
    int ecode = 0;

    el->pollset = cio_new_pollset();
    el->need_stop = 0;
    el->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    el->timer_actions = NULL;
    el->poll_timeout_ms = -1;
    memset(&el->self_id, 0, sizeof(el->self_id));

    el->fd_set = cio_new_hash_set(expected_capacity / 0.75, user_fd_cb_ctx_cmp,
        user_fd_cb_ctx_hash_data, user_fd_cb_ctx_release);

    if (!el->fd_set) {
        ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    if (pipe(el->event_pipe))
        goto fail;

    if (toggle_fd_blocking(el->event_pipe[0], 1))
        goto fail;

    if ((ecode = cio_pollset_add(el->pollset, el->event_pipe[0], CIO_FLAG_IN)))
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
    struct timer_cb_ctx *tctx = el->timer_actions, *tmp_tctx;

    cio_free_pollset(el->pollset);
    cio_free_hash_set(el->fd_set);
    close(el->event_pipe[0]);
    close(el->event_pipe[1]);
    pthread_mutex_destroy(&el->mutex);
    while (tctx) {
        tmp_tctx = tctx;
        tctx = tctx->next;
        free(tmp_tctx);
    }

    free(loop);
}

static void pollset_cb(void *ctx, int fd, int flags)
{
    struct event_loop *el = (struct event_loop *) ctx;;
    int ecode = 0;
    int event_code;
    struct user_fd_cb_ctx search_fctx, *fctx;

    if (fd == el->event_pipe[0]) {
        assert(flags & CIO_FLAG_IN);
        if (flags & CIO_FLAG_ERR || !(flags & CIO_FLAG_IN)) {
            ecode = CIO_READ_ERROR;
            goto fail;
        }

        while (read(fd, &event_code, sizeof(event_code)) > 0) {
            switch (event_code) {
            case STOP:
                el->need_stop = 1;
                return;
            case WAKE_UP:
                return;
            default:
                assert(0);
                ecode = CIO_READ_ERROR;
                goto fail;
            }
        }

        switch (errno) {
            case EAGAIN:
                break;
            default:
                goto fail;
        }
    } else {
        search_fctx.fd = fd;
        if (!(fctx = cio_hash_set_get(el->fd_set, &search_fctx))) {
            ecode = CIO_NOT_FOUND_ERROR;
            goto fail;
        }
        fctx->cb(fctx->ctx, fd, flags);
    }

    return;

fail:
    if (ecode)
        cio_perror(ecode, "pollset_cb");
    else
        perror("pollset_cb");
}

static long long now_time_ms()
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) < 0)
        goto fail;

    return time_ms(&tv);

fail:
    perror("now_time_ms");
    return -1LL;
}

int cio_event_loop_run(void *loop)
{
    struct event_loop *el = (struct event_loop *) loop;
    int ecode = 0, cio_ecode = 0;
    int poll_timeout_ms = -1;
    long long now;
    struct timer_cb_ctx *tctx, *prev_tctx;

    el->self_id = pthread_self();
    do {
        if ((cio_ecode = cio_pollset_poll(el->pollset, poll_timeout_ms, el, pollset_cb)) == -1)
            goto fail;

        if (el->need_stop)
            break;

        if ((ecode = pthread_mutex_lock(&el->mutex)))
            goto fail;

        now = now_time_ms();
        tctx = el->timer_actions;
        while (tctx && tctx->due_time <= now) {
            if ((ecode = pthread_mutex_unlock(&el->mutex)))
                goto fail;

            tctx->action(tctx->action_ctx);

            if ((ecode = pthread_mutex_lock(&el->mutex)))
                goto fail;

            prev_tctx = tctx;
            tctx = tctx->next;
            if (prev_tctx == el->timer_actions)
                el->timer_actions = tctx;
            free(prev_tctx);
        }

        if (tctx)
            poll_timeout_ms = CIO_MAX(tctx->due_time - now_time_ms(), 0);
        else
            poll_timeout_ms = -1;

        if ((ecode = pthread_mutex_unlock(&el->mutex)))
            goto fail;
    } while (1);

    return CIO_NO_ERROR;

fail:
    pthread_mutex_unlock(&el->mutex);

    if (cio_ecode) {
        cio_perror(ecode, "cio_event_loop_run");
        return cio_ecode;
    } else {
        errno = ecode;
        perror("cio_event_loop_run");
        return ecode;
    }
}

static int send_pipe_event(void *loop, int code)
{
    struct event_loop *el = (struct event_loop *) loop;;
    return write(el->event_pipe[1], &code, sizeof(code)) > 0 ? CIO_NO_ERROR : CIO_WRITE_ERROR;
}

int cio_event_loop_stop(void *loop)
{
    return send_pipe_event(loop, STOP);
}

struct add_remove_ctx {
    void *loop;
    int fd;
    int flags;
    void *cb_ctx;
    pollset_cb_t cb;
};

static void add_fd_impl(void *ctx)
{
    struct add_remove_ctx *actx = (struct add_remove_ctx *) ctx;
    struct event_loop *el = (struct event_loop *) actx->loop;
    struct user_fd_cb_ctx *fd_ctx = malloc(sizeof(struct user_fd_cb_ctx));
    int cio_ecode = 0;

    if (!fd_ctx) {
        cio_ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    fd_ctx->fd = actx->fd;
    fd_ctx->ctx = actx->cb_ctx;
    fd_ctx->cb = actx->cb;

    if ((cio_ecode = cio_pollset_add(el->pollset, actx->fd, actx->flags))) {
        goto fail;
    }

    if (!cio_hash_set_add(el->fd_set, fd_ctx)) {
        cio_ecode = CIO_ALLOC_ERROR;
        goto fail;
    }

    free(actx);
    return;

fail:
     if (cio_ecode)
        cio_perror(cio_ecode, "add_fd_impl");

    free(actx);
    free(fd_ctx);
}

int cio_event_loop_add_fd(void *loop, int fd, int flags, void *cb_ctx, pollset_cb_t cb)
{
    struct add_remove_ctx *actx = malloc(sizeof(struct add_remove_ctx));

    if (!actx)
        return CIO_ALLOC_ERROR;

    actx->cb = cb;
    actx->cb_ctx = cb_ctx;
    actx->fd = fd;
    actx->flags = flags;
    actx->loop = loop;

    return cio_event_loop_add_timer(loop, 0, actx, add_fd_impl);
}

static void remove_fd_impl(void *ctx)
{
    struct add_remove_ctx *actx = (struct add_remove_ctx *) ctx;
    struct event_loop *el = (struct event_loop *) actx->loop;
    struct user_fd_cb_ctx *fd_ctx = NULL, search_fd_ctx;
    int cio_ecode = 0;

    search_fd_ctx.fd = actx->fd;
    if (!(fd_ctx = cio_hash_set_remove(el->fd_set, &search_fd_ctx))) {
        cio_ecode = CIO_NOT_FOUND_ERROR;
        goto fail;
    }

    if ((cio_ecode = cio_pollset_remove(el->pollset, actx->fd)))
        goto fail;

    goto finally;

fail:
    cio_perror(cio_ecode, "remove_fd_impl");

finally:
    free(fd_ctx);
    free(actx);
}

int cio_event_loop_remove_fd(void *loop, int fd)
{
    struct add_remove_ctx *actx = malloc(sizeof(struct add_remove_ctx));

    if (!actx)
        return CIO_ALLOC_ERROR;

    actx->loop = loop;
    actx->fd = fd;

    return cio_event_loop_add_timer(loop, 0, actx, remove_fd_impl);
}

int cio_event_loop_add_timer(void *loop, int timeout_ms, void *cb_ctx, void (*cb)(void *))
{
    struct event_loop *el = (struct event_loop *) loop;
    struct timer_cb_ctx *ta, *prev_ta;
    struct timer_cb_ctx *timer_ctx;
    int ecode = 0;

    if (timeout_ms == 0 && pthread_self() == el->self_id) {
        cb(cb_ctx);
        return 0;
    }

    if (!(timer_ctx = malloc(sizeof(*timer_ctx))))
        goto fail;

    timer_ctx->action = cb;
    timer_ctx->action_ctx = cb_ctx;
    timer_ctx->due_time = now_time_ms() + timeout_ms;
    timer_ctx->next = NULL;

    if ((ecode = pthread_mutex_lock(&el->mutex))) {
        errno = ecode;
        goto fail;
    }

    prev_ta = NULL;
    ta = el->timer_actions;
    while (ta) {
        if (ta->due_time > timer_ctx->due_time) {
            break;
        }
        prev_ta = ta;
        ta = ta->next;
    }

    if (prev_ta)
        prev_ta->next = timer_ctx;
    else
        el->timer_actions = timer_ctx;

    timer_ctx->next = ta;

    if ((ecode = pthread_mutex_unlock(&el->mutex))) {
        errno = ecode;
        goto fail;
    }

    return send_pipe_event(loop, WAKE_UP);

fail:
    pthread_mutex_unlock(&el->mutex);
    free(timer_ctx);
    perror("cio_event_loop_add_timer");
    return errno;
}
