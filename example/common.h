#pragma once

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define BUF_SIZE 1024
#define MIN(a, b) (a) < (b) ? (a) : (b)

extern pthread_t event_loop_thread;
extern pthread_mutex_t mutex;
extern pthread_cond_t cond;

enum connection_result {
    in_progress,
    done,
    failed
};

struct connection_ctx {
    void *connection;
    char file_name[BUF_SIZE];
    int size;
    int transferred;
    int fd;
    char buf[4096];
    int len;
    enum connection_result status;
    struct connection_ctx *next;
};

extern struct connection_ctx *connections;

void connection_ctx_set_status(struct connection_ctx *ctx, enum connection_result status);
void free_connection_ctx(struct connection_ctx *ctx);
void add_connection(struct connection_ctx *ctx);

void *start_event_loop();
