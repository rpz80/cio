#include "event_loop.h"
#include "pollset.h"

struct _event_loop {
    void *pollset;
};

void *cio_new_event_loop()
{

}

void cio_free_event_loop(void *loop)
{

}

int cio_event_loop_start(void *loop)
{

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
