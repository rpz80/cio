#include "hash_set.h"
#include <stdlib.h>

struct node {
    void *data;
    struct node *next;
};

struct hset {
    int (*cmp)(const void *, const void *);
    struct node *data;
    int capacity;
};

void *cio_new_hash_set(int capacity, int (*cmp)(const void *, const void *))
{
    struct hset *s = malloc(sizeof(struct hset));
    if (!s)
        return NULL;
    s->data = calloc(capacity, sizeof(s->data));
    if (!s->data) {
        free(hset);
        return NULL;
    }
    s->capacity = capacity;
    s->cmp = cmp;
    return s;
}

void cio_free_hash_set(void *set)
{
    struct hset *s = (struct hset *)set;
    free(s->data);
    free(s);
}

void *cio_hash_set_add(void *set, void *elem)
{

}

void *cio_hash_set_get(void *set, void *elem)
{

}

void *cio_hash_set_remove(void *set, void *elem)
{

}
