/**
 * cio_tcp_connection test suite. Test server is used to test connection. It is based on the
 * cio_tcp_acceptor and cio_tcp_connection. It echoes back what it receives from the client. Also
 * it may start sending messages right after the connection has been established to test full duplex
 * capabilities.
 */

#include "tcp_connection_ut.h"
#include <cio_tcp_connection.h>
#include <cio_tcp_acceptor.h>
#include <cio_event_loop.h>
#include <ct.h>
#include <stdlib.h>
#include <string.h>

/**
 * Test server code.
 */
struct test_server_ctx {
    void *connection;
    void *acceptor;
    char read_buf[BUFSIZ];
    char write_buf[BUFSIZ];
    int duplex_mode_on;
};

static void free_server_ctx(struct test_server_ctx *server_ctx)
{
    if (server_ctx) {
        cio_free_tcp_acceptor(server_ctx->acceptor);
        free(server_ctx);
    }
}

static struct test_server_ctx *new_test_server_ctx(void *event_loop, void *user_ctx)
{
    struct test_server_ctx *server_ctx;
    
    if (!(server_ctx = malloc(sizeof(*server_ctx))))
        goto fail;
    
    memset(server_ctx, 0, sizeof(*server_ctx));
    if (!(server_ctx->acceptor = cio_new_tcp_acceptor(event_loop, user_ctx)))
        goto fail;
    
    return server_ctx;
    
fail:
    free_server_ctx(server_ctx);
    return NULL;
}

/**
 * Tests context.
 */
struct connection_tests_ctx {
    void *connection;
    struct test_server_ctx *test_server_ctx;
    void *event_loop;
};

static const char *const VALID_SERVER_ADDR = "0.0.0.0";
static const int VALID_SERVER_PORT = 23654;

static void free_connection_tests_ctx(struct connection_tests_ctx *test_ctx)
{
    if (test_ctx) {
        cio_free_tcp_connection(test_ctx->connection);
        free_server_ctx(test_ctx->test_server_ctx);
        cio_free_event_loop(test_ctx->event_loop);
    }
}

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
    
    test_ctx->test_server_ctx = malloc(

    return test_ctx;
    
fail:
    free_connection_tests_ctx(test_ctx);
    return NULL;
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
