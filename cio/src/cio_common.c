#include "cio_common.h"
#include "cio_event_loop.h"
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

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

void *new_completion_ctx(void *wrapped_ctx)
{
    struct completion_ctx * completion_ctx;
    
    if (!(completion_ctx = malloc(sizeof(*completion_ctx))))
        return NULL;
    
    completion_ctx->type = COMPLETION;
    completion_ctx->wrapped_ctx = wrapped_ctx;
    completion_ctx->cond = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
    completion_ctx->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    
    return completion_ctx;
}

void free_completion_ctx(void *ctx)
{
    struct completion_ctx *completion_ctx = ctx;
    pthread_cond_destroy(&completion_ctx->cond);
    pthread_mutex_destroy(&completion_ctx->mutex);
}

int completion_ctx_post_and_wait(void *ctx, void *event_loop, void (*posted_cb)(void *))
{
    struct completion_ctx *completion_ctx = ctx;
    int ecode;
    
    if ((ecode = pthread_mutex_lock(&completion_ctx->mutex)))
        return ecode;
    
    cio_event_loop_post(event_loop, 0, completion_ctx, posted_cb);
    
    if ((ecode = pthread_cond_wait(&completion_ctx->cond, &completion_ctx->mutex)))
        return ecode;
    
    if ((ecode = pthread_mutex_unlock(&completion_ctx->mutex)))
        return ecode;
    
    return 0;
}
