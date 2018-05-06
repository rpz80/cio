#include "event_loop_ut.h"
#include <event_loop.h>
#include <ct.h>
#include <pthread.h>

struct loop_ctx {
    void *loop;
    pthread_t thread_handle;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

static void *loop_thread_func(void *ctx)
{
    struct loop_ctx *lctx = (struct loop_ctx *) ctx;
    return (void *) cio_event_loop_start(lctx->loop);
}

int setup_event_loop_tests(void **ctx)
{
    struct loop_ctx *lctx = calloc(1, sizeof(struct loop_ctx));
    int result;
    lctx->loop = cio_new_event_loop();
    if ((result = pthread_create(&lctx->thread_handle, NULL, loop_thread_func, lctx))) {
        perror("thread_create");
        return result;
    }
    return 0;
}

int teardown_event_loop_tests(void **ctx)
{
    struct loop_ctx *lctx = (struct loop_ctx *) *ctx;
    int result;
    void *thread_result;

    cio_event_loop_stop(lctx->loop);
    if ((result = pthread_join(lctx->thread_handle, &thread_result))) {
        perror("join");
        return result;
    }
    cio_free_event_loop(*ctx);
    return 0;
}

static void test_add_cb(void *ctx, int fd, int flags)
{
    struct loop_ctx *lctx = (struct loop_ctx *) ctx;

    ASSERT_EQ_INT(fileno(stdin), fd);
    ASSERT_TRUE(flags & CIO_FLAG_IN);
    ASSERT_TRUE(flags & CIO_FLAG_OUT);

     if (pthread_mutex_lock(&lctx->mutex)) {
        perror("lock");
        return;
    }

    *((int *) ctx) = 1;
    if (pthread_cond_signal(&lctx->cond)) {
        pthread_mutex_unlock(&lctx->mutex);
        perror("signal");
    }

    if (pthread_mutex_unlock(&lctx->mutex))
        perror("unlock");
}

void test_event_loop_add_fd(void **ctx)
{
    int result, added_check = 0;
    struct loop_ctx *lctx = (struct loop_ctx *) *ctx;

    result = cio_event_loop_add_fd(lctx->loop, fileno(stdin), &added_check, test_add_cb);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);

    if ((result = pthread_mutex_lock(&lctx->mutex))) {
        perror("lock");
        return;
    }

    while (added_check == 0) {
        if ((result = pthread_cond_wait(&lctx->cond, &lctx->mutex))) {
            pthread_mutex_unlock(&lctx->mutex);
            perror("wait");
            return;
        }
    }

    if ((result = pthread_mutex_unlock(&lctx->mutex)))
        perror("unlock");

    ASSERT_EQ_INT(1, added_check);
}

