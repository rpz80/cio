#include "common.h"
#include <cio_event_loop.h>
#include <cio_tcp_connection.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

pthread_t event_loop_thread;

static void *event_loop_run_func(void *ctx)
{
    return (void *) cio_event_loop_run(ctx);
}

void *start_event_loop()
{
    void *event_loop;
    int ecode;

    event_loop = cio_new_event_loop(1024);
    if (!event_loop) {
        printf("Error creating event loop\n");
        return NULL;
    }

    if ((ecode = pthread_create(&event_loop_thread, NULL, event_loop_run_func, event_loop))) {
        errno = ecode;
        perror("pthread_create");
        cio_free_event_loop(event_loop);
        return NULL;
    }

    return event_loop;
}

int parse_addr_string(const char *input_string, char *host_addr_buf, int host_addr_buf_len,
    int *port)
{
    const char *ptr;
    int addrlen;

    memset(host_addr_buf, 0, host_addr_buf_len);
    ptr = strstr(input_string, ":");
    if (!ptr || ptr == input_string || *(ptr + 1) == '\0'
            || ((*port = (int) strtol(ptr + 1, NULL, 10)) == 0 && errno != 0)) {
        return -1;
    }

    addrlen = MIN((size_t) (ptr - input_string), host_addr_buf_len - 1);
    strncat(host_addr_buf, input_string, addrlen);

    return 0;
}

const char *path_join(const char *p1, const char *p2, char *out_buf, int out_buf_len)
{
    memset(out_buf, 0, BUFSIZ);
    strncat(out_buf, p1, out_buf_len - 1);
    if (out_buf[strlen(out_buf) - 1] != '/')
        strncat(out_buf, "/", out_buf_len - strlen(out_buf) - 1);
    strncat(out_buf, p2, out_buf_len - strlen(out_buf) - 1);
    return out_buf;
}