#if !defined(CIO_EVENT_LOOP_H)
#define CIO_EVENT_LOOP_H

#include "cio_common.h"

void *cio_new_event_loop(int expected_capacity);
void cio_free_event_loop(void *loop);

int cio_event_loop_run(void *loop);
int cio_event_loop_stop(void *loop);

/**
 * Check CIO_FLAGS for possible flags value.
 */
int cio_event_loop_add_fd(void *loop, int fd, int flags, void *cb_ctx, pollset_cb_t cb);
int cio_event_loop_remove_fd(void *loop, int fd);

/**
 * Posts the callback to the event loop. It implies that callback is always executed on the event
 * loop thread.
 */
int cio_event_loop_post(void *loop, int timeout_ms, void *cb_ctx, void (*cb)(void *));

/**
 * If the caller's thread is the same as the event loop thread, executes callback immediately,
 * otherwise posts it to the event loop.
 */
int cio_event_loop_dispatch(void *loop, void *cb_ctx, void (*cb)(void *));

#endif /* CIO_EVENT_LOOP_H */

