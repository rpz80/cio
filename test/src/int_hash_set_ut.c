#include "int_hash_set_ut.h"
#include <hash_set.h>
#include <stdlib.h>
#include <ct.h>

static int int_cmp(const void *l, const void *r)
{
    return *((int *) l) == *((int *) r);
}

static void int_hash_data(const void *elem, void **data, int *len)
{
    *data = (void *) elem;
    *len = sizeof(int);
}

static void release_int(void *elem)
{
    free(elem);
}

int setup_int_hash_set_tests_with_release(void **ctx)
{
    void *set = cio_new_hash_set(1024, int_cmp, int_hash_data, release_int);

    if (!set)
        return -1;

    *ctx = set;
    return 0;
}

int teardown_int_hash_set_tests(void **ctx)
{
    cio_free_hash_set(*ctx);
    return 0;
}

void test_int_hash_set_w_release(void **ctx)
{
    int *elem = malloc(sizeof(int)), *elem1 = malloc(sizeof(int));
    int not_existing_elem = 43;

    *elem = 42;
    *elem1 = 84;

    ASSERT_NE_PTR(NULL, cio_hash_set_add(*ctx, elem));
    ASSERT_EQ_PTR(NULL, cio_hash_set_add(*ctx, elem));
    ASSERT_NE_PTR(NULL, cio_hash_set_add(*ctx, elem1));

    ASSERT_EQ_INT(42, *((int *) cio_hash_set_get(*ctx, elem)));
    ASSERT_EQ_INT(84, *((int *) cio_hash_set_get(*ctx, elem1)));
    ASSERT_EQ_PTR(NULL, cio_hash_set_get(*ctx, &not_existing_elem));

    elem = cio_hash_set_remove(*ctx, elem);
    ASSERT_NE_PTR(NULL, elem);
    ASSERT_EQ_PTR(NULL, cio_hash_set_remove(*ctx, elem));

    free(elem);
}

/* #TODO test same values (linked list working correctly) */
