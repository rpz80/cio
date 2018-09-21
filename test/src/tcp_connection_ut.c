/**
 * cio_tcp_connection test suite.
 */

#include "tcp_connection_ut.h"
#include <cio_tcp_connection.h>
#include <cio_tcp_server.h>
#include <cio_event_loop.h>
#include <ct.h>
#include <stdlib.h>
#include <string.h>

struct connection_tests_ctx {
    void *connection;
    void *tcp_server;
    void *event_loop;
};

static const char *const VALID_SERVER_ADDR = "0.0.0.0";
static const char *const VALID_SERVER_PORT = "0.0.0.0";

/**
 * Preparational stuff.
 */

static void free_connection_tests_ctx(struct connection_tests_ctx *test_ctx);

static struct connection_tests_ctx *new_connection_tests_ctx()
{
    struct connection_tests_ctx* test_ctx;
    
    test_ctx = malloc(sizeof(*test_ctx));
    if (!test_ctx)
        goto fail;
    
    memset(test_ctx, 0, sizeof(*test_ctx));
    if (!(test_ctx->event_loop = cio_new_event_loop(1024)))
        goto fail;
    
    if (!(test_ctx->connection = cio_new_tcp_connection(test_ctx->event_loop, test_ctx)))
        goto fail;

    return test_ctx;
    
fail:
    free_connection_tests_ctx(test_ctx);
    return NULL;
}

static void free_connection_tests_ctx(struct connection_tests_ctx *test_ctx)
{
    if (test_ctx) {
        cio_free_tcp_connection(test_ctx->connection);
        cio_free_tcp_acceptor(test_ctx->tcp_server);
        cio_free_event_loop(test_ctx->event_loop);
    }
}

int setup_tcp_connnection_tests(void **ctx)
{
    if (!(*ctx = new_connection_tests_ctx()))
        return 1;
    
    return 0;
}

int teardown_tcp_connnection_tests(void **ctx)
{
    ASSERT_NE_PTR(NULL, ((struct connection_tests_ctx *)(*ctx))->connection);
    return 0;
}
/**
 * Assertive, preconditional and conditional auxiliary functions.
 */

/**
 * Test server related code. It echoes back what it receives from the client. Also it may start
 * sending messages right after the connection has been established to test full duplex
 * capabilities.
 */

struct test_server_ctx {
    struct connection_tests_ctx *test_ctx;
    const char *addr;
    int port;
    int duplex_mode_on;
};

static void new_test_server_ctx();
static void free_test_server_ctx();

static void given_echo_tcp_server_started(struct test_server_ctx *test_server_ctx)
{
    
}


/**
 * Tests themselves.
 */

/**
 * Test that the connection object is at least can be created.
 */
void test_new_tcp_connection(void **ctx)
{
    free_connection_tests_ctx(*ctx);
}

void test_tcp_connection_connect_correct_address(void **ctx)
{
    struct connection_tests_ctx* test_ctx = *ctx;
    
    given_echo_tcp_server_started();
}
