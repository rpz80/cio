#include "common.h"
#include <cio_event_loop.h>
#include <cio_tcp_connection.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>

pthread_t event_loop_thread;

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
