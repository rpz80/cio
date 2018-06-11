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
};

static const char *const HOST = "127.0.0.1";
static const int PORT = 54100;

int setup_tcp_server_client_tests(void **ctx)
{
    struct test_ctx *tctx = malloc(sizeof(struct test_ctx));
    int i;

    tctx->client_count = 2;
    tctx->send_count = 10;
    tctx->clients_event_loop = cio_new_event_loop(tctx->client_count);
    ASSERT_NE_PTR(NULL, tctx->clients_event_loop);

    tctx->server_event_loop = cio_new_event_loop(1);
    ASSERT_NE_PTR(NULL, tctx->server_event_loop);

    tctx->clients = malloc(tctx->client_count*(sizeof(void *)));
    ASSERT_NE_PTR(NULL, tctx->clients);

    for (i = 0; i < tctx->client_count; ++i) {
        tctx->clients[i] = new_echo_client(tctx->clients_event_loop, tctx->send_count, HOST, PORT);
        ASSERT_NE_PTR(NULL, tctx->clients[i]);
    }

    return -1;
}

int teardown_tcp_server_client_tests(void **ctx)
{
    return -1;
}

void test_tcp_server_client(void **ctx)
{

}
