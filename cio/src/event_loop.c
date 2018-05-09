#include "event_loop.h"
#include "pollset.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

struct user_fd_cb_ctx {
    int fd;
    pollset_cb_t in_cb;
    pollset_cb_t out_cb;
};

struct event_loop {
    void *pollset;
    int need_stop;
    int event_pipe[2];
};

void *cio_new_event_loop()
{
    struct event_loop *result = malloc(sizeof(struct event_loop));
    int ecode = 0;

    result->pollset = cio_new_pollset();
    result->need_stop = 0;

    if (pipe(result->event_pipe))
        goto fail;

    if ((ecode = cio_pollset_add(result->pollset, result->event_pipe[0], CIO_FLAG_IN)))
        goto fail;

    return result;

fail:
    if (ecode)
        cio_perror(ecode);
    else
        perror("cio_new_event_loop");

    cio_free_event_loop(result);
    return NULL;
}

void cio_free_event_loop(void *loop)
{
    struct event_loop *result = (struct event_loop *) loop;
    cio_free_pollset(result->pollset);
    free(loop);
}

static void pollset_cb(void *ctx, int fd, int flags)
{

}

int cio_event_loop_start(void *loop)
{
    struct event_loop *l = (struct event_loop *) loop;
    while (1) {
        cio_pollset_poll(l->pollset, -1, l, pollset_cb);
    }
}

int cio_event_loop_stop(void *loop)
{

}

int cio_event_loop_add_fd(void *loop, int fd, void *cb_ctx, pollset_cb_t cb)
{

}

int cio_event_loop_remove_fd(void *loop, int fd)
{

}

int cio_event_loop_add_timer(void *loop, int timeout_ms, void *cb_ctx, void (*cb)(void *))
{

}
