#include "pollset_ut.h"
#include <pollset.h>
#include <ct.h>

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

    result = cio_pollset_add(*ctx, 1, CIO_READ | CIO_WRITE);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);
    ASSERT_EQ_INT(1, cio_pollset_size(*ctx));

    result = cio_pollset_add(*ctx, 1, CIO_WRITE);
    ASSERT_EQ_INT(CIO_ALREADY_EXISTS_ERROR, result);
    ASSERT_EQ_INT(1, cio_pollset_size(*ctx));
}

void test_pollset_remove(void **ctx)
{
    int result;

    result = cio_pollset_add(*ctx, 1, CIO_READ | CIO_WRITE);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);
    ASSERT_EQ_INT(1, cio_pollset_size(*ctx));

    result = cio_pollset_remove(*ctx, 1);
    ASSERT_EQ_INT(CIO_NO_ERROR, result);
    ASSERT_EQ_INT(0, cio_pollset_size(*ctx));

    result = cio_pollset_remove(*ctx, 1);
    ASSERT_EQ_INT(CIO_NOT_FOUND_ERROR, result);
    ASSERT_EQ_INT(0, cio_pollset_size(*ctx));
}
