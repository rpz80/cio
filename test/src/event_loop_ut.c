#include "event_loop_ut.h"
#include <event_loop.h>
#include <ct.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

struct loop_ctx {
    void *loop;
    pthread_t thread_handle;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int cb_called;
    int should_unsubscribe_from_cb;
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
    int ecode = 0;

    lctx->loop = cio_new_event_loop(1024);
    if (!lctx->loop)
        goto fail;

    lctx->cond = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
    lctx->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    lctx->cb_called = 0;
    lctx->should_unsubscribe_from_cb = 0;

    ASSERT_EQ_INT(0, pipe(lctx->test_pipe));

    if ((ecode = pthread_create(&lctx->thread_handle, NULL, loop_thread_func, lctx)))
        goto fail;

    *ctx = lctx;
    return 0;

fail:
    if (!lctx->loop)
        cio_perror(CIO_ALLOC_ERROR, NULL);
    else {
        errno = ecode;
        perror("setup_event_loop_tests");
    }

    return ecode;
}

int teardown_event_loop_tests(void **ctx)
{
    struct loop_ctx *lctx = (struct loop_ctx *) *ctx;
    void *thread_result;
    int ecode = 0;

    cio_event_loop_stop(lctx->loop);
    if ((ecode = pthread_join(lctx->thread_handle, &thread_result)))
        goto fail;

    cio_free_event_loop(lctx->loop);
    close(lctx->test_pipe[0]);
    close(lctx->test_pipe[1]);

    if ((ecode = pthread_mutex_destroy(&lctx->mutex)))
        goto fail;

    if ((ecode = pthread_cond_destroy(&lctx->cond)))
        goto fail;

    free(lctx);
    return 0;

fail:
    free(lctx);
    errno = ecode;
    perror("teardown_event_loop_tests");

    return ecode;
}

/**************************************************************************************************/

static const char send_buf[] = "hello";

static void test_add_cb(void *ctx, int fd, int flags)
{
    struct loop_ctx *lctx = (struct loop_ctx *) ctx;
    char rcv_buf[128];
    int ecode = 0;

    ASSERT_TRUE(flags & CIO_FLAG_IN);
    ASSERT_EQ_INT(sizeof(send_buf), read(fd, rcv_buf, sizeof(rcv_buf)));

    if ((ecode = pthread_mutex_lock(&lctx->mutex)))
        goto fail;

    lctx->cb_called = 1;
    if ((ecode = pthread_cond_signal(&lctx->cond)))
        goto fail;

    if (lctx->should_unsubscribe_from_cb) {
        cio_event_loop_remove_fd(lctx->loop, lctx->test_pipe[0]);
        lctx->cb_called = 0;
    }

    if ((ecode = pthread_mutex_unlock(&lctx->mutex)))
        goto fail;

    return;

fail:
    errno = ecode;
    perror("test_add_cb");
    pthread_mutex_unlock(&lctx->mutex);
}

static void when_written_to_the_pipe(struct loop_ctx *lctx)
{
    ASSERT_LT_INT(0, write(lctx->test_pipe[1], send_buf, sizeof(send_buf)));
}

static void when_pipe_fd_added_to_loop(struct loop_ctx *lctx, int should_unsubscribe_from_cb)
{

    int ecode = cio_event_loop_add_fd(lctx->loop, lctx->test_pipe[0], CIO_FLAG_IN, lctx, test_add_cb);
    ASSERT_EQ_INT(CIO_NO_ERROR, ecode);
}

static void when_pipe_fd_removed_from_loop(struct loop_ctx *lctx)
{
    int ecode = cio_event_loop_remove_fd(lctx->loop, lctx->test_pipe[0]);
    ASSERT_EQ_INT(CIO_NO_ERROR, ecode);

    if ((ecode = pthread_mutex_lock(&lctx->mutex)))
        goto fail;

    lctx->cb_called = 0;
    if ((ecode = pthread_mutex_unlock(&lctx->mutex)))
        goto fail;

    return;

fail:
    errno = ecode;
    perror("assert_cb_not_called");
    pthread_mutex_unlock(&lctx->mutex);
}

static void assert_cb_called(struct loop_ctx *lctx)
{
    int ecode = 0;

    if ((ecode = pthread_mutex_lock(&lctx->mutex)))
        goto fail;

    while (lctx->cb_called == 0) {
        if ((ecode = pthread_cond_wait(&lctx->cond, &lctx->mutex)))
            goto fail;
    }

    ASSERT_EQ_INT(1, lctx->cb_called);
    lctx->cb_called = 0;

    if ((ecode = pthread_mutex_unlock(&lctx->mutex)))
        goto fail;

    return;

fail:
    errno = ecode;
    perror("assert_cb_called");
    pthread_mutex_unlock(&lctx->mutex);
}

static void assert_cb_not_called(struct loop_ctx *lctx)
{
    int ecode = 0;

    if ((ecode = pthread_mutex_lock(&lctx->mutex)))
        goto fail;

    ASSERT_EQ_INT(0, lctx->cb_called);
    if ((ecode = pthread_mutex_unlock(&lctx->mutex)))
        goto fail;

    return;

fail:
    errno = ecode;
    perror("assert_cb_not_called");
    pthread_mutex_unlock(&lctx->mutex);
}

/**************************************************************************************************/

void test_event_loop_add_remove(void **ctx)
{
    struct loop_ctx *lctx = (struct loop_ctx *) *ctx;

    when_pipe_fd_added_to_loop(lctx, 0);
    when_written_to_the_pipe(lctx);
    assert_cb_called(lctx);

    when_pipe_fd_removed_from_loop(lctx);
    when_written_to_the_pipe(lctx);
    assert_cb_not_called(lctx);
}

void test_event_loop_add_remove_from_cb(void **ctx)
{
    struct loop_ctx *lctx = (struct loop_ctx *) *ctx;

    when_pipe_fd_added_to_loop(lctx, 1);
    when_written_to_the_pipe(lctx);
    assert_cb_called(lctx);

    when_written_to_the_pipe(lctx);
    assert_cb_not_called(lctx);
}

/* #TODO: remove/add fd  from cb ut */
/* #TODO: timer function ut */
