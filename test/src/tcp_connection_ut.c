#include "tcp_connection_ut.h"
#include <cio_tcp_connection.h>
#include <cio_tcp_acceptor.h>
#include <cio_event_loop.h>
#include <ct.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

/**
 * Test server code.
 */
struct test_server_ctx {
    void *connection;
    void *acceptor;
    char read_buf[BUFSIZ];
    char write_buf[BUFSIZ];
};

static void free_server_ctx(struct test_server_ctx *server_ctx)
{
    if (server_ctx) {
        cio_free_tcp_connection_sync(server_ctx->connection);
        cio_free_tcp_acceptor_sync(server_ctx->acceptor);
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
    pthread_t event_loop_thread;
    int accepted;
    int connected;
    int duplex_on;
    pthread_mutex_t mutex;
};

static const char *const VALID_SERVER_ADDR = "0.0.0.0";
static const int VALID_SERVER_PORT = 23654;

static void free_connection_tests_ctx(struct connection_tests_ctx *test_ctx)
{
    void *result;

    if (test_ctx) {
        cio_free_tcp_connection_sync(test_ctx->connection);
        free_server_ctx(test_ctx->test_server_ctx);
        cio_event_loop_stop(test_ctx->event_loop);
        ASSERT_EQ_INT(0, pthread_join(test_ctx->event_loop_thread, &result));
        cio_free_event_loop(test_ctx->event_loop);
        pthread_mutex_destroy(&test_ctx->mutex);
        free(test_ctx);
    }
}

static void *event_loop_run_func(void *ctx)
{
    return (void *) cio_event_loop_run(ctx);
}

static struct connection_tests_ctx *new_connection_tests_ctx()
{
    struct connection_tests_ctx* test_ctx;
    int ecode;
    
    test_ctx = malloc(sizeof(*test_ctx));
    if (!test_ctx)
        goto fail;
    
    memset(test_ctx, 0, sizeof(*test_ctx));
    if (!(test_ctx->event_loop = cio_new_event_loop(1024)))
        goto fail;
    
    if (!(test_ctx->connection = cio_new_tcp_connection(test_ctx->event_loop, test_ctx)))
        goto fail;
    
    if (!(test_ctx->test_server_ctx = new_test_server_ctx(test_ctx->event_loop, test_ctx)))
        goto fail;

    test_ctx->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    ecode = pthread_create(&test_ctx->event_loop_thread, NULL, event_loop_run_func,
                           test_ctx->event_loop);
    if (ecode) {
        errno = ecode;
        perror("pthread_create");
        goto fail;
    }
    
    return test_ctx;
    
fail:
    free_connection_tests_ctx(test_ctx);
    return NULL;
}

/**
 * Setup() & Teardown(). Assertive, preconditional and conditional auxiliary functions.
 */
int setup_tcp_connnection_tests(void **ctx)
{
    if (!(*ctx = new_connection_tests_ctx()))
        return 1;
    
    return 0;
}

int teardown_tcp_connnection_tests(void **ctx)
{
    struct connection_tests_ctx *test_ctx = *ctx;

    free_connection_tests_ctx(test_ctx);
    return 0;
}

static void on_accept(int fd, void *ctx, int ecode)
{
    struct connection_tests_ctx *tests_ctx = ctx;
    ASSERT_EQ_INT(ecode, CIO_NO_ERROR);
    ASSERT_NE_INT(fd, -1);
    tests_ctx->test_server_ctx->connection = cio_new_tcp_connection_connected_fd(
        tests_ctx->event_loop, tests_ctx, fd);
    ASSERT_NE_PTR(NULL, tests_ctx->test_server_ctx->connection);
    pthread_mutex_lock(&tests_ctx->mutex);
    tests_ctx->accepted = 1;
    pthread_mutex_unlock(&tests_ctx->mutex);
}

static void on_connect(void *ctx, int ecode)
{
    struct connection_tests_ctx *tests_ctx = ctx;
    ASSERT_EQ_INT(ecode, CIO_NO_ERROR);
    pthread_mutex_lock(&tests_ctx->mutex);
    tests_ctx->connected = 1;
    pthread_mutex_unlock(&tests_ctx->mutex);
}

static void when_echo_tcp_server_started(struct connection_tests_ctx *tests_ctx, const char *addr,
                                         int port)
{
    cio_tcp_acceptor_async_accept(tests_ctx->test_server_ctx->acceptor, addr, port, on_accept);
}

static void when_connection_attempt_is_made(struct connection_tests_ctx *tests_ctx,
                                            const char *addr, int port)
{
    cio_tcp_connection_async_connect(tests_ctx->connection, addr, port, on_connect);
}

static void when_duplex_mode_is(struct connection_tests_ctx *tests_ctx, int on)
{
    tests_ctx->duplex_on = on;
}

static void then_both_side_connections_are_successful(struct connection_tests_ctx *tests_ctx)
{
    while (1) {
        pthread_mutex_lock(&tests_ctx->mutex);
        if (tests_ctx->accepted == 1 && tests_ctx->connected == 1) {
            pthread_mutex_unlock(&tests_ctx->mutex);
            break;
        }
        pthread_mutex_unlock(&tests_ctx->mutex);
        usleep(5 * 1000);
    }
}

static void when_data_transfer_is_started(struct connection_tests_ctx *tests_ctx)
{
    
}

static void then_all_data_transferred_correctly(struct connection_tests_ctx *tests_ctx)
{
    
}

/**
 * Tests.
 */
void test_new_tcp_connection(void **ctx)
{
    ASSERT_NE_PTR(NULL, ((struct connection_tests_ctx *)(*ctx))->connection);
}

void test_tcp_connection_connect_correct_address(void **ctx)
{
    struct connection_tests_ctx* test_ctx = *ctx;
    
    when_duplex_mode_is(test_ctx, 1);
    when_echo_tcp_server_started(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    when_connection_attempt_is_made(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    then_both_side_connections_are_successful(test_ctx);
}

void test_tcp_connection_read_write_duplex_success(void **ctx)
{
    struct connection_tests_ctx* test_ctx = *ctx;
    
    when_duplex_mode_is(test_ctx, 1);
    when_echo_tcp_server_started(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    when_connection_attempt_is_made(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    then_both_side_connections_are_successful(test_ctx);
    
    when_data_transfer_is_started(test_ctx);
    then_all_data_transferred_correctly(test_ctx);
}
