#if !defined(CIO_EVENT_LOOP_H)
#define CIO_EVENT_LOOP_H

#include "common.h"

void *cio_new_event_loop(int expected_capacity);
void cio_free_event_loop(void *loop);

int cio_event_loop_run(void *loop);
int cio_event_loop_stop(void *loop);
int cio_event_loop_add_fd(void *loop, int fd, int flags, void *cb_ctx, pollset_cb_t cb);
int cio_event_loop_remove_fd(void *loop, int fd);
int cio_event_loop_add_timer(void *loop, int timeout_ms, void *cb_ctx, void (*cb)(void *));

#endif // CIO_EVENT_LOOP_H

