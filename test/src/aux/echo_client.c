#include "echo_client.h"
#include <tcp_client.h>
#include <event_loop.h>
#include <resolv.h>
#include <ct.h>
#include <stdlib.h>

struct client_ctx {
    void *tcp_client;
    char send_buf[512];
    char rcv_buf[512];
    int send_count; /* -1 for infinite loop */
    int received_count;
};

static const char *const send_buf_prefix = "hello from ";

void *new_echo_client(void *event_loop, int send_count, const char *addr_string, int port)
{
    struct client_ctx *cctx;
    int fd, addrlen;
    struct sockaddr sock_addr;

    cctx = malloc(sizeof(*cctx));
    ASSERT_NE_PTR(NULL, cctx);

    fd = cio_resolve(addr_string, port, AF_INET, &sock_addr, &addrlen);
    ASSERT_NE_INT(-1, fd);

    cctx->tcp_client = cio_new_tcp_client(event_loop, cctx, fd, &sock_addr, addrlen);
    ASSERT_NE_PTR(NULL, cctx->tcp_client);
    cctx->received_count = 0;
    cctx->send_count = send_count;

    return cctx;
}

void free_echo_client(void *echo_client)
{
    struct client_ctx *cctx = echo_client;

    cio_free_tcp_client(cctx->tcp_client);
    free(cctx);
}

static void on_write(void *ctx, int ecode, int written)
{

}

static void on_connect(void *ctx, int ecode)
{
    struct client_ctx *cctx = ctx;

    ASSERT_EQ_INT(CIO_NO_ERROR, ecode);
    fprintf(stdout, "client %p connected\n", cctx->tcp_client);

    strncpy(cctx->send_buf, send_buf_prefix, strlen(send_buf_prefix));
    snprintf(cctx->send_buf + strlen(cctx->send_buf),
        sizeof(cctx->send_buf) - strlen(cctx->send_buf), "%p\n", cctx->tcp_client);
    cio_tcp_client_async_write(cctx->tcp_client, cctx->send_buf, strlen(cctx->send_buf), on_write);
}

void echo_client_start(void *echo_client)
{
    struct client_ctx *cctx = echo_client;

    cio_tcp_client_async_connect(cctx->tcp_client, on_connect);
}

