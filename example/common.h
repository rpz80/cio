#pragma once

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define MIN(a, b) (a) < (b) ? (a) : (b)

extern pthread_t event_loop_thread;

void *start_event_loop();
int parse_addr_string(const char *input_string, char *host_addr_buf, int host_addr_buf_len,
    int *port);
