#pragma once

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define BUF_SIZE 1024
#define MIN(a, b) (a) < (b) ? (a) : (b)

extern pthread_t event_loop_thread;

void *start_event_loop();
