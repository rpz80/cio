#include "int_hash_set_ut.h"
#include <cio_hash_set.h>
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
    /* no need to free(elem1) as it should be destroyed along with the set itself */
}

/* ---------------------------------------------------------------------------------------------- */

static void int_hash_data_always_same(const void *elem, void **data, int *len)
{
    (void) elem;
    static int i = 3;

    *data = &i;
    *len = sizeof(i);
}

int setup_int_hash_set_tests_linked_list(void **ctx)
{
    void *set = cio_new_hash_set(1024, int_cmp, int_hash_data_always_same, release_int);

    if (!set)
        return -1;

    *ctx = set;
    return 0;
}

void test_int_hash_set_linked_list(void **ctx)
{
    int *elem = malloc(sizeof(int)), *elem1 = malloc(sizeof(int)), *elem2 = malloc(sizeof(int));

    *elem = 42;
    *elem1 = 84;
    *elem2 = 168;

    ASSERT_NE_PTR(NULL, cio_hash_set_add(*ctx, elem));
    ASSERT_EQ_PTR(NULL, cio_hash_set_add(*ctx, elem));

    ASSERT_NE_PTR(NULL, cio_hash_set_add(*ctx, elem1));
    ASSERT_EQ_PTR(NULL, cio_hash_set_add(*ctx, elem1));

    ASSERT_NE_PTR(NULL, cio_hash_set_add(*ctx, elem2));
    ASSERT_EQ_PTR(NULL, cio_hash_set_add(*ctx, elem2));

    ASSERT_EQ_INT(42, *((int *) cio_hash_set_get(*ctx, elem)));
    ASSERT_EQ_INT(84, *((int *) cio_hash_set_get(*ctx, elem1)));
    ASSERT_EQ_INT(168, *((int *) cio_hash_set_get(*ctx, elem2)));

    elem1 = cio_hash_set_remove(*ctx, elem1);
    ASSERT_NE_PTR(NULL, elem1);

    free(elem1);
}
