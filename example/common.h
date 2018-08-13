#pragma once

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define BUF_SIZE 1024
#define MIN(a, b) (a) < (b) ? (a) : (b)

extern pthread_mutex_t mutex;
extern pthread_cond_t cond;

void *start_event_loop();
void wait_for_done(void *event_loop, void *ctx, void (*before_stop_action)(void *));
