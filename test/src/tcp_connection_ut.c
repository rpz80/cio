/**
 * cio_tcp_connection test suite.
 */

#include "tcp_connection_ut.h"
#include <cio_tcp_connection.h>
#include <cio_event_loop.h>
#include <ct.h>
#include <stdlib.h>

struct connection_tests_ctx {
    void *connection;
    void *event_loop;
};

int setup_tcp_connnection_tests(void **ctx)
{
    struct connection_tests_ctx* test_ctx;
    
    test_ctx = malloc(sizeof(*test_ctx));
    test_ctx->event_loop = cio_new_event_loop(1024);
    if (!test_ctx->event_loop) {
        free(test_ctx);
        return -1;
    }
    
    test_ctx->connection = cio_new_tcp_connection(test_ctx->event_loop, test_ctx);
    if (!test_ctx->connection) {
        cio_free_event_loop(test_ctx->event_loop);
        free(test_ctx);
        return -1;
    }
 
    *ctx = test_ctx;
    
    return 0;
}

int teardown_tcp_connnection_tests(void **ctx)
{
    ASSERT_NE_PTR(NULL, ((struct connection_tests_ctx *)(*ctx))->connection);
    return 0;
}

void test_new_tcp_connection(void **ctx)
{
    struct connection_tests_ctx* test_ctx = *ctx;

    cio_free_tcp_connection(test_ctx->connection);
    cio_free_event_loop(test_ctx->event_loop);
}

void test_tcp_connection_connect_correct_address(void **ctx)
{
    struct connection_tests_ctx* test_ctx = *ctx;
}
