#include <tcp_connection.h>
#include <event_loop.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024
#define MIN(a, b) (a) < (b) ? (a) : (b)

static pthread_t event_loop_thread;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

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

static void connection_ctx_set_status(struct connection_ctx *ctx, enum connection_result status)
{
    pthread_mutex_lock(&mutex);
    ctx->status = status;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

static void free_connection_ctx(struct connection_ctx *ctx)
{
    cio_free_tcp_connection(ctx->connection);
    close(ctx->fd);
    free(ctx);
}

static void on_write(void *ctx, int ecode);

static void on_read(void *ctx, int ecode, int bytes_read)
{
    struct connection_ctx *cctx = ctx;
    int file_bytes_read;

    printf("on_read\n");
    if (ecode != CIO_NO_ERROR || bytes_read == 0) {
        cio_perror(ecode, "on_read");
        goto fail;
    }

    if (bytes_read != 4) {
        printf("on_read: received message of unexpected len for file %s, closing\n", cctx->file_name);
        goto fail;
    }

    cctx->transferred += ntohl(*(int*)(cctx->buf));
    printf("File %s: %d%%\n", cctx->file_name, (int) (((double) cctx->transferred / cctx->size) * 100));
    file_bytes_read = read(cctx->fd, cctx->buf, sizeof(cctx->buf));

    if (file_bytes_read == -1) {
        perror("read file");
        goto fail;
    }

    if (file_bytes_read == 0) {
        connection_ctx_set_status(ctx, done);
        return;
    }


    cio_tcp_connection_async_write(cctx->connection, cctx->buf, file_bytes_read, on_write);

    return;

fail:
    connection_ctx_set_status(ctx, failed);
}

static void on_write(void *ctx, int ecode)
{
    struct connection_ctx *cctx = ctx;

    printf("on_write\n");
    if (ecode != CIO_NO_ERROR) {
        connection_ctx_set_status(ctx, failed);
        return;
    }

    cio_tcp_connection_async_read(cctx->connection, cctx->buf, sizeof(cctx->buf), on_read);
}

static void on_connect(void *ctx, int ecode)
{
    struct connection_ctx *connection_ctx = ctx;
    int bytes_read;

    printf("on_connect\n");
    if (ecode != CIO_NO_ERROR) {
        cio_perror(ecode, "Connection failed");
        goto fail;
    }

    int size = htonl(connection_ctx->size);
    memcpy(connection_ctx->buf, &size, sizeof(size));
    bytes_read = read(connection_ctx->fd, connection_ctx->buf + sizeof(size),
        sizeof(connection_ctx->buf) - sizeof(size));

    if (bytes_read == -1) {
        perror("read file");
        goto fail;
    }

    if (bytes_read == 0) {
        connection_ctx_set_status(ctx, done);
        return;
    }

    cio_tcp_connection_async_write(connection_ctx->connection, connection_ctx->buf,
        bytes_read + sizeof(size), on_write);

    return;

fail:
    connection_ctx_set_status(connection_ctx, failed);
}

static void do_send(void *event_loop, struct connection_ctx **connections, const char *addr,
    const char *path, size_t size)
{
    struct connection_ctx *ctx;
    struct connection_ctx *root;
    char buf[BUF_SIZE];
    char *ptr;
    int port;
    size_t addrlen;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        printf("Failed to create connection context for file %s\n", path);
        return;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->size = size;
    ctx->fd = open(path, O_RDONLY, S_IRUSR);
    if (ctx->fd == -1) {
        printf("Failed to open file %s\n", path);
        free(ctx);
        return;
    }

    ctx->connection = cio_new_tcp_connection(event_loop, ctx);
    if (!ctx->connection) {
        printf("Failed to create connection for file %s\n", path);
        close(ctx->fd);
        free(ctx);
        return;
    }

    ptr = strstr(addr, ":");
    if (!ptr || ptr == addr || *(ptr + 1) == '\0' || ((port = (int) strtol(ptr + 1, NULL, 10)) == 0 && errno != 0)) {
        printf("Invalid address string %s for file %s\n", addr, path);
        free_connection_ctx(ctx);
        return;
    }

    strncpy(ctx->file_name, path, BUF_SIZE - 1);
    ctx->file_name[BUF_SIZE - 1] = '\0';

    addrlen = MIN((size_t) (ptr - addr), BUF_SIZE - 1);
    strncpy(buf, addr, addrlen);
    buf[addrlen] = '\0';

    if (*connections == NULL) {
        *connections = ctx;
    } else {
        root = *connections;
        while (root->next)
            root = root->next;
        root->next = ctx;
    }
    cio_tcp_connection_async_connect(ctx->connection, buf, port, on_connect);
}

static int do_work(void *event_loop, struct connection_ctx **connections, const char *addr,
    const char *path)
{
    DIR *dir;
    struct dirent *entry;
    struct stat stat_buf;
    char path_buf[BUF_SIZE];

    if (stat(path, &stat_buf)) {
        printf("Stat failed for %s\n", path);
        return -1;
    }

    if ((stat_buf.st_mode & S_IFMT) == S_IFREG) {
        do_send(event_loop, connections, addr, path, stat_buf.st_size);
    } else if ((stat_buf.st_mode & S_IFMT) == S_IFDIR) {
        if (!(dir = opendir(path))) {
            printf("Failed to open dir %s\n", path);
            return -1;
        }
        while ((entry = readdir(dir)) != NULL) {
            strncpy(path_buf, path, BUF_SIZE);
            path_buf[BUF_SIZE - 1] = '\0';
            strncat(path_buf, entry->d_name, BUF_SIZE - strlen(path_buf) - 1);
            if (stat(path_buf, &stat_buf) || ((stat_buf.st_mode & S_IFMT) != S_IFREG)) {
                printf("Invalid file %s\n", path_buf);
                continue;
            }
            do_send(event_loop, connections, addr, path_buf, stat_buf.st_size);
        }
    } else {
        printf("Invalid path %s\n", path);
        return -1;
    }

    return 0;
}

static void *event_loop_run_func(void *ctx)
{
    return (void *) cio_event_loop_run(ctx);
}

static void *start_event_loop()
{
    void *event_loop;
    int ecode;

    event_loop = cio_new_event_loop(1024);
    if (!event_loop) {
        printf("Error creating event loop\n");
        return NULL;
    }

    if ((ecode = pthread_create(&event_loop_thread, NULL, event_loop_run_func, event_loop))) {
        errno = ecode;
        perror("pthread_create");
        cio_free_event_loop(event_loop);
        return NULL;
    }

    return event_loop;
}

static int has_unfinished_connections(struct connection_ctx *connections)
{
    for (; connections; connections = connections->next) {
        if (connections->status == in_progress)
            return 1;
    }

    return 0;
}

static void wait_for_done(void *event_loop, struct connection_ctx *connections)
{
    int ecode;
    void *thread_result;

    pthread_mutex_lock(&mutex);
    while (has_unfinished_connections(connections))
        pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);

    cio_event_loop_stop(event_loop);
    if ((ecode = pthread_join(event_loop_thread, &thread_result))) {
        errno = ecode;
        perror("pthread_join");
        return;
    }

    printf("Event loop has finished, result: %d\n", (int) thread_result);
}

int main(int argc, char *const argv[])
{
    int opt;
    char path_buf[BUF_SIZE];
    char addr_buf[BUF_SIZE];
    void *event_loop;
    struct connection_ctx *connections, *tmp;

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(path_buf, 0, BUF_SIZE);
    memset(addr_buf, 0, BUF_SIZE);
    while ((opt = getopt(argc, argv, "a:p:h")) != -1) {
        switch (opt) {
        case 'p':
            strncpy(path_buf, optarg, BUF_SIZE - 1);
            path_buf[BUF_SIZE - 1] = '\0';
            break;
        case 'a':
            strncpy(addr_buf, optarg, BUF_SIZE - 1);
            addr_buf[BUF_SIZE - 1] = '\0';
            break;
        case 'h':
            printf("Example tcp client. Sends file(s) from <path> to the specified <address>.\n" \
                   " -a <host:port>\n" \
                   " -p <path>  (Might be a single file or a directory)\n");
            return EXIT_SUCCESS;
        }
    }

    if (!path_buf[0] || !addr_buf[0])
    {
        printf("Required options are mising. Run with -h to get help.\n");
        return EXIT_FAILURE;
    }

    event_loop = start_event_loop();
    if (!event_loop) {
        printf("Failed to create and start event loop. Bailing out.\n");
        return EXIT_FAILURE;
    }

    connections = NULL;
    do_work(event_loop, &connections, addr_buf, path_buf);
    wait_for_done(event_loop, connections);

    while(connections) {
        tmp = connections;
        connections = connections->next;
        free_connection_ctx(tmp);
    }

    cio_free_event_loop(event_loop);
}
