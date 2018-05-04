#if !defined(CIO_POLLSET_H)
#define CIO_POLLSET_H

#include "common.h"

void *cio_new_pollset();
int *cio_free_pollset(void *pollset);
int cio_pollset_add(void *pollset, int fd, int flags);
int cio_pollset_remove(void *pollset, int fd);
int cio_pollset_size(void *pollset);

#endif // CIO_POLLSET_H
