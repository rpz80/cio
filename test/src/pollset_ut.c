#include "pollset_ut.h"
#include <pollset.h>
#include <ct.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

void test_pollset_new(void **ctx)
{
    void *ps = cio_new_pollset();
    ASSERT_TRUE(ps != NULL);
    cio_free_pollset(ps);
}

int setup_pollset_tests(void **ctx)
{
    void *ps = cio_new_pollset();
    ASSERT_TRUE(ps != NULL);
    if (ps == NULL)
        return -1;
    *ctx = ps;

    return 0;
}

int teardown_pollset_tests(void **ctx)
{
    cio_free_pollset(*ctx);
    return 0;
}

void test_pollset_add(void **ctx)
{
    int result;

    result = cio_pollset_add(*ctx, 1, CIO_FLAG_IN | CIO_FLAG_OUT);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);
    ASSERT_EQ_INT(1, cio_pollset_size(*ctx));

    result = cio_pollset_add(*ctx, 1, CIO_FLAG_OUT);
    ASSERT_EQ_INT(CIO_ALREADY_EXISTS_ERROR, result);
    ASSERT_EQ_INT(1, cio_pollset_size(*ctx));
}

void test_pollset_remove(void **ctx)
{
    int result;

    result = cio_pollset_add(*ctx, 1, CIO_FLAG_IN | CIO_FLAG_OUT);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);
    ASSERT_EQ_INT(1, cio_pollset_size(*ctx));

    result = cio_pollset_remove(*ctx, 1);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);
    ASSERT_EQ_INT(0, cio_pollset_size(*ctx));

    result = cio_pollset_remove(*ctx, 1);
    ASSERT_EQ_INT(CIO_NOT_FOUND_ERROR, result);
    ASSERT_EQ_INT(0, cio_pollset_size(*ctx));
}

static void pollset_cb(void *ctx, int fd, int flags)
{
    int *test_pipe = ctx;

    if (fd == test_pipe[0]) {
        ASSERT_TRUE(flags & CIO_FLAG_IN);
        ASSERT_FALSE(flags & CIO_FLAG_OUT);
    } else {
        ASSERT_TRUE(flags & CIO_FLAG_OUT);
        ASSERT_FALSE(flags & CIO_FLAG_IN);
    }
}

/* #TODO socket poll tests */

void test_pollset_poll(void **ctx)
{
    int result;
    int test_pipe[2];
    char buf[] = "hello";

    ASSERT_EQ_INT(0, pipe(test_pipe));
    ASSERT_LT_INT(0, write(test_pipe[1], buf, sizeof(buf)));
    
    result = cio_pollset_add(*ctx, test_pipe[0], CIO_FLAG_IN);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);

    result = cio_pollset_add(*ctx, test_pipe[1], CIO_FLAG_OUT);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);

    ASSERT_EQ_INT(2, cio_pollset_poll(*ctx, -1, test_pipe, pollset_cb));
    close(test_pipe[0]);
    close(test_pipe[1]);
}
