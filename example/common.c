#include "common.h"
#include <event_loop.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>

static pthread_t event_loop_thread;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

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

void wait_for_done(void *event_loop, void *ctx, void (*before_stop_action)(void *))
{
    int ecode;
    void *thread_result;

    before_stop_action(ctx);
    cio_event_loop_stop(event_loop);
    if ((ecode = pthread_join(event_loop_thread, &thread_result))) {
        errno = ecode;
        perror("pthread_join");
        return;
    }

    printf("Event loop has finished, result: %d\n", (int) thread_result);
}
