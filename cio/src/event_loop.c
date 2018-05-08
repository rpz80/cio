#include "event_loop.h"
#include "pollset.h"
#include <stdlib.h>

struct _event_loop {
    void *pollset;
    int need_stop;
};

void *cio_new_event_loop()
{
    struct _event_loop *result = malloc(sizeof(struct _event_loop));
    result->pollset = cio_new_pollset();
    result->need_stop = 0;
    return result;
}

void cio_free_event_loop(void *loop)
{
    struct _event_loop *result = (struct _event_loop *) loop;
    cio_free_pollset(result->pollset);
    free(loop);
}

int cio_event_loop_start(void *loop)
{
    struct _event_loop *l = (struct _event_loop *) loop;
    while (!l->need_stop) {
    
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
