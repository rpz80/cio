#include "pollset.h"
#include "common.h"
#include "config.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#if defined (HAVE_EPOLL_H)
static void *new_pollset()
{
    return NULL;
}
#endif

#if defined (HAVE_POLL_H)
#include <sys/poll.h>
#include <sys/types.h>

struct pollset {
    struct pollfd *pollfds;
    int size;
    int capacity;
    int used;
};

static const int INITIAL_CAPACITY = 256;

static void *new_pollset()
{
    struct pollset *result = malloc(sizeof(struct pollset));
    result->size = 0;
    result->used = 0;
    result->capacity = INITIAL_CAPACITY;
    result->pollfds = calloc(INITIAL_CAPACITY, sizeof(struct pollfd));

    return result;
}

static int *free_pollset(void *pollset)
{
    struct pollset *ps = (struct pollset *) pollset;
    free(ps->pollfds);
    free(pollset);
    return CIO_NO_ERROR;
}

static int pollset_add(void *pollset, int fd, int flags)
{
    struct pollset *ps = (struct pollset *) pollset;
    int unreserved_index = -1;

    for (int i = 0; i < ps->size; ++i) {
        if (ps->pollfds[i].fd == fd)
            return CIO_ALREADY_EXISTS_ERROR;
        if (ps->pollfds[i].fd == -1)
            unreserved_index = i;
    }

    unreserved_index = unreserved_index == -1 ? ps->size : unreserved_index;
    assert(unreserved_index <= ps->capacity);
    if (unreserved_index == ps->capacity) {
        ps->capacity *= 2;
        ps->pollfds = realloc(ps->pollfds, sizeof(struct pollfd) * ps->capacity);
        if (ps->pollfds == NULL)
            return CIO_ALLOC_ERROR;
    }
    ps->pollfds[unreserved_index].fd = fd;
    ps->pollfds[unreserved_index].revents = 0;
    if (flags & CIO_FLAG_IN)
        ps->pollfds[unreserved_index].events = POLLIN;
    if (flags & CIO_FLAG_OUT)
        ps->pollfds[unreserved_index].events |= POLLOUT;
#if defined (_GNU_SOURCE)
    ps->pollfds[unreserved_index].events |= POLLRDHUP;
#endif
    ps->used++;
    if (unreserved_index == ps->size)
        ps->size++;

    return CIO_NO_ERROR;
}

static int pollset_remove(void* pollset, int fd)
{
    struct pollset *ps = (struct pollset *)pollset;
    int i;

    for (i = 0; i < ps->size; ++i) {
        if (ps->pollfds[i].fd == fd) {
            ps->pollfds[i].events = 0;
            ps->pollfds[i].fd = -1;
            ps->used--;
            return CIO_NO_ERROR;
        }
    }

    return CIO_NOT_FOUND_ERROR;
}

static int pollset_size(void *pollset)
{
    struct pollset *ps = (struct pollset *)pollset;
    return ps->used;
}

static int pollset_poll(void *pollset, int timeout_ms, void *cb_ctx, pollset_cb_t cb)
{
    struct pollset *ps = (struct pollset *)pollset;
    int result, i, flags;

    result = poll(ps->pollfds, ps->size, timeout_ms);
    if (result == -1) {
        perror("poll");
        return -1;
    } else if (result == 0) {
        return 0;
    } else {
        for (i = 0; i < ps->size; ++i) {
            if (ps->pollfds[i].revents != 0) {
                if (ps->pollfds[i].revents | POLLIN)
                    flags = CIO_FLAG_IN;
                if (ps->pollfds[i].revents | POLLOUT)
                    flags |= CIO_FLAG_OUT;
                if (ps->pollfds[i].revents | POLLERR)
                    flags |= CIO_FLAG_ERR;
#if defined (_GNU_SOURCE)
                if (ps->pollfds[i].revents | POLLRDHUP)
                    flags |= CIO_FLAG_RDHUP;
#endif
                cb(cb_ctx, ps->pollfds[i].fd, flags);
            }
        }
    }

    return result;
}

#else
#endif // HAVE_POLL_H

void *cio_new_pollset()
{
    return new_pollset();
}

int *cio_free_pollset(void *pollset)
{
    return free_pollset(pollset);
}

int cio_pollset_add(void *pollset, int fd, int flags)
{
    return pollset_add(pollset, fd, flags);
}

int cio_pollset_remove(void *pollset, int fd)
{
    return pollset_remove(pollset, fd);
}

int cio_pollset_size(void *pollset)
{
    return pollset_size(pollset);
}

int cio_pollset_poll(void *pollset, int timeout_ms, void *cb_ctx, pollset_cb_t cb)
{
    return pollset_poll(pollset, timeout_ms, cb_ctx, cb);
}
