#include "struct_hash_set_ut.h"
#include <cio_hash_set.h>
#include <ct.h>
#include <stddef.h>

typedef struct {
    double value;
    int key;
} test_struct_t;

static int cmp(const void *l, const void *r)
{
    return ((test_struct_t *) l)->key == ((test_struct_t *) r)->key;
}

static void hash_data(const void *elem, void **data, int *len)
{
    test_struct_t *ts = (test_struct_t *) elem;

    *data = &ts->key;
    *len = sizeof(ts->key);
}

int setup_struct_hash_set_tests_without_release(void **ctx)
{
    void *set = cio_new_hash_set(1024, cmp, hash_data, NULL);

    if (!set)
        return -1;

    *ctx = set;
    return 0;
}

int teardown_struct_hash_set_tests(void **ctx)
{
    cio_free_hash_set(*ctx);
    return 0;
}

void test_struct_hash_set_without_release(void **ctx)
{
    test_struct_t *elem = malloc(sizeof(test_struct_t)), *elem1 = malloc(sizeof(test_struct_t));
    test_struct_t not_existing_elem = {12.0, 56};

    elem->key = 42;
    elem->value = 102.2;
    elem1->key = 84;
    elem1->value = 33.33;

    ASSERT_NE_PTR(NULL, cio_hash_set_add(*ctx, elem));
    ASSERT_EQ_PTR(NULL, cio_hash_set_add(*ctx, elem));
    ASSERT_NE_PTR(NULL, cio_hash_set_add(*ctx, elem1));

    ASSERT_EQ_INT(102.2, ((test_struct_t *) cio_hash_set_get(*ctx, elem))->value);
    ASSERT_EQ_INT(33.33, ((test_struct_t *) cio_hash_set_get(*ctx, elem1))->value);
    ASSERT_EQ_PTR(NULL, cio_hash_set_get(*ctx, &not_existing_elem));

    elem = cio_hash_set_remove(*ctx, elem);
    ASSERT_NE_PTR(NULL, elem);
    ASSERT_EQ_PTR(NULL, cio_hash_set_remove(*ctx, elem));

    free(elem);
    free(elem1);
}
