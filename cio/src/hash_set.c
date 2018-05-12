#include "hash_set.h"
#include <stdlib.h>
#include <string.h>

struct node {
    void *data;
    struct node *next;
};

static struct node *new_node(void *data, struct node *first,
    int (*cmp)(const void *, const void *))
{
    struct node *n = NULL;
    struct node *tmp = NULL;

    for (tmp = first; tmp; tmp = tmp->next) {
        if (cmp(data, tmp->data))
            return NULL;
    }

    n = malloc(sizeof(struct node));
    if (!n)
        return NULL;

    n->next = NULL;
    n->data = data;

    if (first) {
        tmp = first;
        while (tmp->next)
            tmp = tmp->next;
        tmp->next = n;
        return first;
    }

    return n;
}

static void free_node(struct node *n, void (*release)(void *))
{
    if (!n)
        return;

    while (n->next) {
        free_node(n->next, release);
    }

    if (release)
        release(n->data);

    free(n);
}

static void *remove_node(struct node **first, const void* data,
    int (*cmp)(const void *, const void *))
{
    struct node *prev = NULL;
    struct node *f = *first;
    void *result = NULL;

    if (!f)
        return result;

    while (f) {
        if (cmp(data, f->data)) {
            result = f->data;
            if (prev) {
                prev->next = f->next;
            } else if (f->next) {
                *first = f->next;
            } else {
                *first = NULL;
            }
            free(f);
            break;
        }
        prev = f;
        f = f->next;
    }

    return result;
}

static void *get_node_data(const struct node *first, const void *elem,
    int (*cmp)(const void *, const void *))
{
    for ( ; first; first = first->next) {
        if (cmp(first->data, elem))
            return first->data;
    }

    return NULL;
}

static unsigned jenkins_hash(const void *elem,
    void (*hash_data)(const void *elem, void **data, int *len))
{
    unsigned i = 0;
    unsigned hash = 0;
    char *key;
    int len;

    hash_data(elem, (void **) &key, &len);
    while (i != len) {
        hash += key[i++];
        hash += hash << 10;
        hash ^= hash >> 6;
    }

    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;

    return hash;
}

struct hset {
    int (*cmp)(const void *, const void *);
    void (*hash_data)(const void *elem, void **data, int *len);
    void (*release)(void *);
    struct node **nodes;
    unsigned capacity;
};

void *cio_new_hash_set(unsigned capacity, int (*cmp)(const void *, const void *),
    void (*hash_data)(const void *elem, void **data, int *len), void (*release)(void *))
{
    struct hset *s = malloc(sizeof(struct hset));

    if (!s)
        return NULL;

    s->nodes = calloc(capacity, sizeof(*s->nodes));
    if (!s->nodes) {
        free(s);
        return NULL;
    }

    memset(s->nodes, 0, sizeof(*s->nodes)*capacity);
    s->capacity = capacity;
    s->cmp = cmp;
    s->hash_data = hash_data;
    s->release = release;

    return s;
}

void cio_free_hash_set(void *set)
{
    struct hset *s = (struct hset *)set;
    int i;

    for (i = 0; i < s->capacity; ++i) {
        if (s->nodes[i])
            free_node(s->nodes[i], s->release);
    }

    free(s->nodes);
    free(s);
}

void *cio_hash_set_add(void *set, void *elem)
{
    struct hset *s = (struct hset *)set;
    struct node *n = NULL;
    unsigned pos = jenkins_hash(elem, s->hash_data) % s->capacity;

    n = new_node(elem, s->nodes[pos], s->cmp);
    if (!n)
        return NULL;

    s->nodes[pos] = n;
    return elem;
}

void *cio_hash_set_get(void *set, const void *elem)
{
    struct hset *s = (struct hset *)set;
    unsigned pos = jenkins_hash(elem, s->hash_data) % s->capacity;
    return get_node_data(s->nodes[pos], elem, s->cmp);
}

void *cio_hash_set_remove(void *set, void *elem)
{
    struct hset *s = (struct hset *)set;
    unsigned pos = jenkins_hash(elem, s->hash_data) % s->capacity;
    return remove_node(&s->nodes[pos], elem, s->cmp);
}
