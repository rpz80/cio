#include "cio_common.h"
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

void cio_perror(enum CIO_ERROR error, const char *message)
{
#define PRINT_ERROR(m, text) \
    do { \
        m ? fprintf(stderr, "CIO: "text": %s\n", m) : fprintf(stderr, "CIO: "text"\n"); \
    } while (0)

    switch (error)
    {
    case CIO_NO_ERROR:              PRINT_ERROR(message, "no error"); break;
    case CIO_UNKNOWN_ERROR:         PRINT_ERROR(message, "unknown error"); break;
    case CIO_ALREADY_EXISTS_ERROR:  PRINT_ERROR(message, "already exists"); break;
    case CIO_ALLOC_ERROR:           PRINT_ERROR(message, "allocation error"); break;
    case CIO_NOT_FOUND_ERROR:       PRINT_ERROR(message, "not found"); break;
    case CIO_WRITE_ERROR:           PRINT_ERROR(message, "write error"); break;
    case CIO_READ_ERROR:            PRINT_ERROR(message, "read error"); break;
    case CIO_POLL_ERROR:            PRINT_ERROR(message, "poll error"); break;
    case CIO_WRONG_STATE_ERROR:     PRINT_ERROR(message, "wrong state error"); break;
    case CIO_ERROR_COUNT:           assert(0); break;
    };

#undef PRINT_ERROR
}

long long time_ms(struct timeval *tv)
{
    return (long long) tv->tv_sec*1000 + tv->tv_usec/1000;
}

int toggle_fd_nonblocking(int fd, int on)
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

    return 0;

fail:
    perror("toggle_fd_blocking");
    return errno;
}
