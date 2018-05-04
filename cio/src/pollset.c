#include "pollset.h"
#include "common.h"
#include "config.h"
#include <stdlib.h>
#include <assert.h>

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
};

static const int INITIAL_CAPACITY = 256;

static void *new_pollset()
{
    struct pollset *result = malloc(sizeof(struct pollset));
    result->size = 0;
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
    int i;
    struct pollset *ps = (struct pollset *) pollset;

    for (i = 0; i < ps->size; ++i) {
        if (ps->pollfds[i].fd == fd)
            return CIO_ALREADY_EXISTS_ERROR;
    }

    assert(ps->size <= ps->capacity);
    if (ps->size == ps->capacity) {
        ps->capacity *= 2;
        ps->pollfds = realloc(ps->pollfds, sizeof(struct pollfd) * ps->capacity);
        if (ps->pollfds == NULL)
            return CIO_ALLOC_ERROR;
    }
    ps->pollfds[ps->size].fd = fd;
    ps->pollfds[ps->size].revents = 0;
    if (flags & CIO_READ)
        ps->pollfds[ps->size].events = POLLIN;
    if (flags & CIO_WRITE)
        ps->pollfds[ps->size].events |= POLLOUT;
#if defined (_GNU_SOURCE)
    ps->pollfds[ps->size].events |= POLLRDHUP;
#endif
    ps->size++;

    return CIO_NO_ERROR;
}

static int pollset_size(void *pollset)
{
    struct pollset *ps = (struct pollset *)ps;
    return ps->size;
}

#else
#endif // HAVE_POLL_H

void *cio_new_pollset()
{
    return new_pollset();
}

int *cio_free_pollset(void *pollset)
{
    free_pollset(pollset);
}

int cio_pollset_add(void *pollset, int fd, int flags)
{
    return pollset_add(pollset, fd, flags);
}

int cio_pollset_remove(void *pollset, int fd)
{

}

int cio_pollset_size(void *pollset)
{
    return pollset_size(pollset);
}
