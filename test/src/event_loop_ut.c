#include "event_loop_ut.h"
#include <event_loop.h>
#include <ct.h>
#include <pthread.h>
#include <unistd.h>

struct loop_ctx {
    void *loop;
    pthread_t thread_handle;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int cb_called;
    int test_pipe[2];
};

static void *loop_thread_func(void *ctx)
{
    struct loop_ctx *lctx = (struct loop_ctx *) ctx;
    return (void *) cio_event_loop_run(lctx->loop);
}

int setup_event_loop_tests(void **ctx)
{
    struct loop_ctx *lctx = calloc(1, sizeof(struct loop_ctx));
    int ecode = -1;
    const char *error_message = NULL;

    lctx->loop = cio_new_event_loop(1024);
    if (!lctx->loop)
        goto fail;

    lctx->cond = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
    lctx->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    lctx->cb_called = 0;

    ASSERT_EQ_INT(0, pipe(lctx->test_pipe));

    if ((ecode = pthread_create(&lctx->thread_handle, NULL, loop_thread_func, lctx))) {
        error_message = "thread_create";
        goto fail;
    }

    *ctx = lctx;
    return 0;

fail:
    if (!lctx->loop)
        cio_perror(CIO_ALLOC_ERROR, NULL);
    else
        perror(error_message);

    return ecode;
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
    cio_free_event_loop(lctx->loop);
    close(lctx->test_pipe[0]);
    close(lctx->test_pipe[1]);
    pthread_mutex_destroy(&lctx->mutex);
    pthread_cond_destroy(&lctx->cond);
    free(lctx);

    return 0;
}

static const char send_buf[] = "hello";

static void test_add_cb(void *ctx, int fd, int flags)
{
    struct loop_ctx *lctx;
    char rcv_buf[128];

    lctx = (struct loop_ctx *) ctx;

    ASSERT_TRUE(flags & CIO_FLAG_IN);
    ASSERT_EQ_INT(sizeof(send_buf), read(fd, rcv_buf, sizeof(rcv_buf)));

    if (pthread_mutex_lock(&lctx->mutex)) {
        perror("lock");
        return;
    }

    lctx->cb_called = 1;
    if (pthread_cond_signal(&lctx->cond)) {
        pthread_mutex_unlock(&lctx->mutex);
        perror("signal");
    }

    if (pthread_mutex_unlock(&lctx->mutex))
        perror("unlock");
}

void test_event_loop_add_fd(void **ctx)
{
    int result;
    struct loop_ctx *lctx;

    lctx = (struct loop_ctx *) *ctx;

    ASSERT_LT_INT(0, write(lctx->test_pipe[1], send_buf, sizeof(send_buf)));
    
    result = cio_event_loop_add_fd(lctx->loop, lctx->test_pipe[0], CIO_FLAG_IN, lctx, test_add_cb);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);

    if ((result = pthread_mutex_lock(&lctx->mutex))) {
        perror("lock");
        return;
    }

    while (lctx->cb_called == 0) {
        if ((result = pthread_cond_wait(&lctx->cond, &lctx->mutex))) {
            pthread_mutex_unlock(&lctx->mutex);
            perror("wait");
            return;
        }
    }

    if ((result = pthread_mutex_unlock(&lctx->mutex)))
        perror("unlock");

    ASSERT_EQ_INT(1, lctx->cb_called);
}

