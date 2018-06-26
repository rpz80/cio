#include "tcp_server_client_ut.h"
#include "aux/echo_client.h"
#include <tcp_client.h>
#include <tcp_server.h>
#include <ct.h>
#include <event_loop.h>
#include <stdlib.h>
#include <pthread.h>

struct test_ctx {
    void **clients;
    int client_count;
    int send_count;
    void *clients_event_loop;
    void *server_event_loop;
    pthread_t clients_event_loop_thread;
};

static const char *const HOST = "ya.ru";
static const int PORT = 80;

static void *clients_event_loop_thread_func(void *ctx)
{
    struct test_ctx *tctx = ctx;
    return (void *) cio_event_loop_run(tctx->clients_event_loop);
}

int setup_tcp_server_client_tests(void **ctx)
{
    struct test_ctx *tctx = malloc(sizeof(struct test_ctx));
    int i;

    tctx->client_count = 1;
    tctx->send_count = 10;
    tctx->clients_event_loop = cio_new_event_loop(tctx->client_count);
    ASSERT_NE_PTR(NULL, tctx->clients_event_loop);
    pthread_create(&tctx->clients_event_loop_thread, NULL, clients_event_loop_thread_func, tctx);

    tctx->server_event_loop = cio_new_event_loop(1);
    ASSERT_NE_PTR(NULL, tctx->server_event_loop);

    tctx->clients = malloc(tctx->client_count*(sizeof(void *)));
    ASSERT_NE_PTR(NULL, tctx->clients);

    for (i = 0; i < tctx->client_count; ++i) {
        tctx->clients[i] = cio_new_tcp_client(tctx->clients_event_loop, tctx);
        ASSERT_NE_PTR(NULL, tctx->clients[i]);
    }

    *ctx = tctx;
    return 0;
}

int teardown_tcp_server_client_tests(void **ctx)
{
    struct test_ctx *tctx = *ctx;
    int i;
    void *celt_result;

    pthread_join(tctx->clients_event_loop_thread, &celt_result);
    for (i = 0; i < tctx->client_count; ++i)
        cio_free_tcp_client(tctx->clients[i]);

    return 0;
}

static void on_client_connect(void *ctx, int ecode)
{
    struct test_ctx *tctx = ctx;

    switch (ecode) {
    case CIO_NO_ERROR:
        fprintf(stdout, "Connected\n");
        break;
    default:
        fprintf(stdout, "Connect failed\n");
        break;
    }
}

void test_tcp_server_client(void **ctx)
{
    struct test_ctx *tctx = *ctx;
    cio_tcp_client_async_connect(tctx->clients[0], HOST, PORT, on_client_connect);
}
