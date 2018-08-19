#include "common.h"
#include <cio_event_loop.h>
#include <cio_tcp_connection.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>

pthread_t event_loop_thread;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void connection_ctx_set_status(struct connection_ctx *ctx, enum connection_result status)
{
    pthread_mutex_lock(&mutex);
    ctx->status = status;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

void free_connection_ctx(struct connection_ctx *ctx)
{
    cio_free_tcp_connection(ctx->connection);
    close(ctx->fd);
    free(ctx);
}

static void *event_loop_run_func(void *ctx)
{
    return (void *) cio_event_loop_run(ctx);
}

void *start_event_loop()
{
    void *event_loop;
    int ecode;

    event_loop = cio_new_event_loop(1024);
    if (!event_loop) {
        printf("Error creating event loop\n");
        return NULL;
    }

    if ((ecode = pthread_create(&event_loop_thread, NULL, event_loop_run_func, event_loop))) {
        errno = ecode;
        perror("pthread_create");
        cio_free_event_loop(event_loop);
        return NULL;
    }

    return event_loop;
}
