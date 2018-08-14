#if !defined(CIO_POLLSET_H)
#define CIO_POLLSET_H

#include "cio_common.h"

void *cio_new_pollset();
void cio_free_pollset(void *pollset);
int cio_pollset_add(void *pollset, int fd, int flags);
int cio_pollset_remove(void *pollset, int fd);
int cio_pollset_size(void *pollset);
/**
 * timeout_ms < 0 - infinite timeout.
 * timeout_ms == 0 - do not wait, just poll.
 */
int cio_pollset_poll(void *pollset, int timeout_ms, void *cb_ctx, pollset_cb_t cb);

#endif /* CIO_POLLSET_H */
