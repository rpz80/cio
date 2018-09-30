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

struct growable_buffer {
    char *data;
    int size;
    int capacity;
};

void free_growable_buffer(struct growable_buffer *growable_buffer)
{
    if (growable_buffer) {
        free(growable_buffer->data);
        free(growable_buffer);
    }
}

struct growable_buffer *new_growable_buffer(int capacity)
{
    struct growable_buffer *growable_buffer;
    
    if (!(growable_buffer = malloc(sizeof(*growable_buffer))))
        goto fail;
    
    if (!(growable_buffer->data = malloc(capacity)))
        goto fail;
    
    growable_buffer->size = 0;
    growable_buffer->capacity = capacity;
    
    return growable_buffer;
    
fail:
    free_growable_buffer(growable_buffer);
    return NULL;
}

int growable_buffer_append(struct growable_buffer *growable_buffer, const void *data, int size)
{
    int new_capacity, new_size;
    
    new_capacity = growable_buffer->capacity;
    new_size = growable_buffer->size + size;
    if (new_size > new_capacity) {
        while (new_size > new_capacity)
            new_capacity *= 2;
        if (!(growable_buffer->data = realloc(growable_buffer->data, new_capacity)))
            return -1;
    }
    memcpy(growable_buffer->data + growable_buffer->size, data, size);
    growable_buffer->size = new_size;
    
    return 0;
}

struct test_client {
    void *connection;
    struct connection_tests *tests_fixture;
    int connected;
    int written;
    char read_buf[1024];
    struct growable_buffer *total_read_buf;
    pthread_mutex_t mutex;
};

static void free_test_client(struct test_client *test_client)
{
    if (test_client) {
        cio_free_tcp_connection_sync(test_client->connection);
        free_growable_buffer(test_client->total_read_buf);
        free(test_client);
    }
}

static struct test_client *new_test_client(void *event_loop,
                                               struct connection_tests *tests_fixture,
                                               int create_connection)
{
    struct test_client *test_client;
    
    test_client = malloc(sizeof(*test_client));
    if (!test_client)
        goto fail;
    
    memset(test_client, 0, sizeof(*test_client));
    test_client->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    test_client->tests_fixture = tests_fixture;
    if (create_connection
        && !(test_client->connection = cio_new_tcp_connection(event_loop, test_client)))
        goto fail;
    
    if (!(test_client->total_read_buf = new_growable_buffer(BUFSIZ)))
        goto fail;
    
    return test_client;
    
fail:
    free_test_client(test_client);
    return NULL;
}

struct test_server {
    struct test_client *server_client;
    void *acceptor;
    int accepted;
};

static void free_server_ctx(struct test_server *server_ctx)
{
    if (server_ctx) {
        free_test_client(server_ctx->server_client);
        cio_free_tcp_acceptor_sync(server_ctx->acceptor);
        free(server_ctx);
    }
}

static struct test_server *new_test_server(void *event_loop,
                                               struct connection_tests* tests_fixture)
{
    struct test_server *test_server;
    
    if (!(test_server = malloc(sizeof(*test_server))))
        goto fail;
    
    memset(test_server, 0, sizeof(*test_server));
    if (!(test_server->acceptor = cio_new_tcp_acceptor(event_loop, tests_fixture)))
        goto fail;
    
    if (!(test_server->server_client = new_test_client(event_loop, tests_fixture, 0)))
        goto fail;

    return test_server;
    
fail:
    free_server_ctx(test_server);
    return NULL;
}

/**
 * Tests context.
 */
struct connection_tests {
    struct test_server *test_server;
    struct test_client *test_client;
    void *event_loop;
    pthread_t event_loop_thread;
    pthread_mutex_t mutex;
    int duplex_on;
    char *test_data;
    int test_data_size;
};

static const char *const VALID_SERVER_ADDR = "0.0.0.0";
static const int VALID_SERVER_PORT = 23654;

static void free_connection_tests_ctx(struct connection_tests *test_ctx)
{
    void *result;

    if (test_ctx) {
        free_test_client(test_ctx->test_client);
        free_server_ctx(test_ctx->test_server);
        cio_event_loop_stop(test_ctx->event_loop);
        ASSERT_EQ_INT(0, pthread_join(test_ctx->event_loop_thread, &result));
        cio_free_event_loop(test_ctx->event_loop);
        pthread_mutex_destroy(&test_ctx->mutex);
        free(test_ctx->test_data);
        free(test_ctx);
    }
}

static void *event_loop_run_func(void *ctx)
{
    return (void *) cio_event_loop_run(ctx);
}

static void *generate_test_data(int data_size)
{
    char *data;
    int i;
    
    if (!(data = malloc(data_size)))
        return NULL;
    
    srand(time(NULL));
    for (i = 0; i < data_size; ++i)
        data[i] = rand() % 256;
    
    return data;
}

static struct connection_tests *new_connection_tests_fixture()
{
    struct connection_tests* tests_fixture;
    int ecode;
    
    tests_fixture = malloc(sizeof(*tests_fixture));
    if (!tests_fixture)
        goto fail;
    
    memset(tests_fixture, 0, sizeof(*tests_fixture));
    if (!(tests_fixture->event_loop = cio_new_event_loop(1024)))
        goto fail;


    if (!(tests_fixture->test_client = new_test_client(tests_fixture->event_loop, tests_fixture, 1)))
        goto fail;
    
    if (!(tests_fixture->test_server = new_test_server(tests_fixture->event_loop, tests_fixture)))
        goto fail;
    
    tests_fixture->test_data_size = 1024 * 1024 * 10;
    if (!(tests_fixture->test_data = generate_test_data(tests_fixture->test_data_size)))
        goto fail;

    tests_fixture->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    ecode = pthread_create(&tests_fixture->event_loop_thread, NULL, event_loop_run_func,
                           tests_fixture->event_loop);
    if (ecode) {
        errno = ecode;
        perror("pthread_create");
        goto fail;
    }
    
    return tests_fixture;
    
fail:
    free_connection_tests_ctx(tests_fixture);
    return NULL;
}

/**
 * Setup() & Teardown(). Assertive, preconditional and conditional auxiliary functions.
 */
int setup_tcp_connnection_tests(void **ctx)
{
    if (!(*ctx = new_connection_tests_fixture()))
        return 1;
    
    return 0;
}

int teardown_tcp_connnection_tests(void **ctx)
{
    struct connection_tests *test_ctx = *ctx;

    free_connection_tests_ctx(test_ctx);
    return 0;
}

static void on_accept(int fd, void *ctx, int ecode)
{
    struct connection_tests *tests_fixture = ctx;
    
    ASSERT_EQ_INT(ecode, CIO_NO_ERROR);
    ASSERT_NE_INT(fd, -1);
    
    tests_fixture->test_server->server_client->connection = cio_new_tcp_connection_connected_fd(
        tests_fixture->event_loop, tests_fixture->test_server->server_client, fd);
    ASSERT_NE_PTR(NULL, tests_fixture->test_server->server_client->connection);
    
    pthread_mutex_lock(&tests_fixture->mutex);
    tests_fixture->test_server->accepted = 1;
    pthread_mutex_unlock(&tests_fixture->mutex);
}

static void on_connect(void *ctx, int ecode)
{
    struct test_client *test_client = ctx;
    struct connection_tests *tests_fixture = test_client->tests_fixture;
    
    ASSERT_EQ_INT(ecode, CIO_NO_ERROR);
    
    pthread_mutex_lock(&tests_fixture->mutex);
    test_client->connected = 1;
    pthread_mutex_unlock(&tests_fixture->mutex);
}

static void when_echo_tcp_server_started(struct connection_tests *tests_ctx, const char *addr,
                                         int port)
{
    cio_tcp_acceptor_async_accept(tests_ctx->test_server->acceptor, addr, port, on_accept);
}

static void when_connection_attempt_is_made(struct connection_tests *tests_ctx,
                                            const char *addr, int port)
{
    cio_tcp_connection_async_connect(tests_ctx->test_client->connection, addr, port, on_connect);
}

static void when_duplex_mode_is(struct connection_tests *tests_ctx, int on)
{
    tests_ctx->duplex_on = on;
}

static void then_both_side_connections_are_successful(struct connection_tests *tests_ctx)
{
    while (1) {
        pthread_mutex_lock(&tests_ctx->mutex);
        if (tests_ctx->test_server->accepted == 1 && tests_ctx->test_client->connected == 1) {
            pthread_mutex_unlock(&tests_ctx->mutex);
            break;
        }
        pthread_mutex_unlock(&tests_ctx->mutex);
        usleep(5 * 1000);
    }
}

static void on_write(void *ctx, int ecode)
{
    int next_write_size;
    struct test_client *test_client = ctx;
    struct connection_tests *tests_fixture = test_client->tests_fixture;
    
    printf("ON WRITE: ecode: %d, test_client: %p\n", ecode, test_client);
    if (test_client->written != tests_fixture->test_data_size)
        ASSERT_EQ_INT(CIO_NO_ERROR, ecode);
    else
        return;
    
    test_client->written += BUFSIZ;
    ASSERT_LE_INT(test_client->written, tests_fixture->test_data_size);
    if (test_client->written == tests_fixture->test_data_size)
        return;
    
    next_write_size = CIO_MIN(BUFSIZ, tests_fixture->test_data_size - test_client->written);
    cio_tcp_connection_async_write(test_client->connection,
                                   tests_fixture->test_data + test_client->written,
                                   next_write_size, on_write);
}

static void on_read(void *ctx, int ecode, int bytes_read)
{
    struct test_client *test_client = ctx;
    struct connection_tests *tests = test_client->tests_fixture;
    
    printf("ON READ: ecode: %d, test_client: %p, bytes_read: %d\n", ecode, test_client, bytes_read);
    ASSERT_EQ_INT(CIO_NO_ERROR, ecode);
    ASSERT_EQ_INT(0, pthread_mutex_lock(&test_client->mutex));
    growable_buffer_append(test_client->total_read_buf, test_client->read_buf, bytes_read);
    ASSERT_EQ_INT(0, pthread_mutex_unlock(&test_client->mutex));
    
    if (test_client->total_read_buf->size == tests->test_data_size) {
        ASSERT_EQ_INT(0, memcmp(test_client->total_read_buf->data, tests->test_data,
                                tests->test_data_size));
        return;
    }
    
    cio_tcp_connection_async_read(test_client->connection, test_client->read_buf,
                                  sizeof(test_client->read_buf), on_read);
}

static void when_data_transfer_is_started(struct connection_tests *tests_ctx)
{
    ASSERT_NE_PTR(NULL, tests_ctx->test_client->connection);
    ASSERT_NE_PTR(NULL, tests_ctx->test_server->server_client->connection);
    
    cio_tcp_connection_async_write(tests_ctx->test_client->connection,
                                   tests_ctx->test_data + tests_ctx->test_client->written,
                                   BUFSIZ, on_write);
    
    cio_tcp_connection_async_read(tests_ctx->test_client->connection,
                                  tests_ctx->test_client->read_buf,
                                  sizeof(tests_ctx->test_client->read_buf), on_read);
    
    cio_tcp_connection_async_write(tests_ctx->test_server->server_client->connection,
                                   tests_ctx->test_data + tests_ctx->test_server->server_client->written,
                                   BUFSIZ, on_write);
    
    cio_tcp_connection_async_read(tests_ctx->test_server->server_client->connection,
                                  tests_ctx->test_server->server_client->read_buf,
                                  sizeof(tests_ctx->test_server->server_client), on_read);

}

static int all_data_read(struct test_client *client, int expected_size)
{
    int ready;
    
    ASSERT_EQ_INT(0, pthread_mutex_lock(&client->mutex));
    ready = client->total_read_buf->size == expected_size;
    ASSERT_EQ_INT(0, pthread_mutex_unlock(&client->mutex));

    return ready;
}

static void then_all_data_transferred_correctly(struct connection_tests *tests_ctx)
{
    while (1) {
        if (all_data_read(tests_ctx->test_client, tests_ctx->test_data_size)
            && all_data_read(tests_ctx->test_server->server_client, tests_ctx->test_data_size))
            break;
        usleep(10 * 1000);
    }
}

/**
 * Tests.
 */
void test_new_tcp_connection(void **ctx)
{
    ASSERT_NE_PTR(NULL, ((struct connection_tests *)(*ctx))->test_client->connection);
}

void test_tcp_connection_connect_correct_address(void **ctx)
{
    struct connection_tests* test_ctx = *ctx;
    
    when_duplex_mode_is(test_ctx, 1);
    when_echo_tcp_server_started(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    when_connection_attempt_is_made(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    then_both_side_connections_are_successful(test_ctx);
}

void test_tcp_connection_read_write_duplex_success(void **ctx)
{
    struct connection_tests* test_ctx = *ctx;
    
    when_duplex_mode_is(test_ctx, 1);
    when_echo_tcp_server_started(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    when_connection_attempt_is_made(test_ctx, VALID_SERVER_ADDR, VALID_SERVER_PORT);
    then_both_side_connections_are_successful(test_ctx);
    
    when_data_transfer_is_started(test_ctx);
    then_all_data_transferred_correctly(test_ctx);
}
