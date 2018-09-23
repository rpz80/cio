#include "../common.h"
#include <cio_tcp_connection.h>
#include <cio_event_loop.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

enum connection_result {
    in_progress,
    done,
    failed
};

struct connection_ctx {
    void *connection;
    char file_name[BUFSIZ];
    ssize_t size;
    int transferred;
    int fd;
    char write_buf[4096];
    char read_buf[4096];
    int len;
    enum connection_result status;
    struct connection_ctx *next;
};

enum {
    MODE_ASYNC,
    MODE_SEQ
} mode;

struct connection_ctx *connections;

void add_connection(struct connection_ctx *connection)
{
    struct connection_ctx *root;

    if (connections == NULL) {
        connections = connection;
    } else {
        root = connections;
        while (root->next)
            root = root->next;
        root->next = connection;
    }
}

void connection_ctx_set_status(struct connection_ctx *ctx, enum connection_result status)
{
    cio_free_tcp_connection_async(ctx->connection);
    ctx->connection = NULL;

    pthread_mutex_lock(&mutex);
    ctx->status = status;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

void free_connection_ctx(struct connection_ctx *ctx)
{
    if (ctx) {
        if (ctx->connection) {
            cio_free_tcp_connection_async(ctx->connection);
            close(ctx->fd);
        }
        free(ctx);
    }
}

static void on_write(void *ctx, int ecode);
static void send_file(struct connection_ctx *ctx, int send_header);

static void on_read(void *ctx, int ecode, int bytes_read)
{
    struct connection_ctx *cctx = ctx;

    if (ecode != CIO_NO_ERROR) {
        cio_perror(ecode, "on_read");
        goto fail;
    }

    if (bytes_read == 0) {
        printf("Connection closed by peer, file: %s\n", cctx->file_name);
        goto fail;
    }

    if (bytes_read != 4) {
        printf("on_read: received message of unexpected len for file %s, closing\n",
            cctx->file_name);
        goto fail;
    }

    cctx->transferred = ntohl(*(int*)(cctx->read_buf));
    printf("\rFile %s: %d%%", cctx->file_name,
        (int) (((double) cctx->transferred / cctx->size) * 100));

    if (cctx->transferred == cctx->size) {
        connection_ctx_set_status(ctx, done);
        printf("\nFile %s transferred successfully\n", cctx->file_name);
        return;
    }

    if (mode == MODE_SEQ)
        send_file(ctx, 0);
    else
        cio_tcp_connection_async_read(cctx->connection, cctx->read_buf, sizeof(cctx->read_buf),
            on_read);

    return;

fail:
    connection_ctx_set_status(ctx, failed);
}

static void on_write(void *ctx, int ecode)
{
    struct connection_ctx *cctx = ctx;

    if (ecode != CIO_NO_ERROR) {
        connection_ctx_set_status(ctx, failed);
        return;
    }

    if (mode == MODE_SEQ)
        cio_tcp_connection_async_read(cctx->connection, cctx->read_buf, sizeof(cctx->read_buf),
            on_read);
    else
        send_file(ctx, 0);
}

static void send_file(struct connection_ctx *ctx, int send_header)
{
    int offset = 0;
    int tmp;
    ssize_t file_bytes_read, file_name_len;

    if (send_header) {
        file_name_len = strlen(ctx->file_name);
        tmp = htonl(file_name_len);
        memcpy(ctx->write_buf, &tmp, sizeof(tmp));
        offset += sizeof(tmp);
        tmp = MIN(file_name_len, sizeof(ctx->write_buf) - offset);
        memcpy(ctx->write_buf + offset, ctx->file_name, tmp);
        offset += tmp;
        tmp = htonl(ctx->size);
        memcpy(ctx->write_buf + offset, &tmp, sizeof(tmp));
        offset += sizeof(tmp);
    }

    file_bytes_read = read(ctx->fd, ctx->write_buf + offset, sizeof(ctx->write_buf) - offset);
    if (file_bytes_read == -1) {
        printf("Error reading file %s\n", ctx->file_name);
        connection_ctx_set_status(ctx, failed);
        return;
    }

    offset += file_bytes_read;
    cio_tcp_connection_async_write(ctx->connection, ctx->write_buf, offset, on_write);
}

static void on_connect(void *ctx, int ecode)
{
    struct connection_ctx *cctx = ctx;

    if (ecode != CIO_NO_ERROR) {
        cio_perror((enum CIO_ERROR) ecode, "Connection failed");
        connection_ctx_set_status(cctx, failed);
        return;
    }

    send_file(ctx, 1);
    if (mode == MODE_ASYNC)
        cio_tcp_connection_async_read(cctx->connection, cctx->read_buf, sizeof(cctx->read_buf), on_read);
}

static void init_connection(void *event_loop, const char *addr, const char *full_path,
    const char *file_name, size_t size)
{
    struct connection_ctx *ctx;
    int port;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        printf("Failed to create connection context for file %s\n", full_path);
        return;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->size = size;
    ctx->fd = open(full_path, O_RDONLY, S_IRUSR);
    if (ctx->fd == -1) {
        printf("Failed to open file %s\n", full_path);
        free(ctx);
        return;
    }

    ctx->connection = cio_new_tcp_connection(event_loop, ctx);
    if (!ctx->connection) {
        printf("Failed to create connection for file %s\n", full_path);
        close(ctx->fd);
        free(ctx);
        return;
    }
    strncat(ctx->file_name, file_name, BUFSIZ - 1);

    if (parse_addr_string(addr, ctx->read_buf, sizeof(ctx->read_buf), &port)) {
        printf("Invalid address string %s for file %s\n", addr, full_path);
        free_connection_ctx(ctx);
        return;
    }

    add_connection(ctx);
    cio_tcp_connection_async_connect(ctx->connection, ctx->read_buf, port, on_connect);
}

static const char *file_name_from_path(const char *path)
{
    const char *ptr = path + strlen(path);

    for (; ptr != path; ptr--) {
        if (*ptr == '/') {
            ++ptr;
            break;
        }
    }

    return ptr;
}

static int do_work(void *event_loop, const char *addr, const char *path)
{
    DIR *dir;
    struct dirent *entry;
    struct stat stat_buf;
    char path_buf[BUFSIZ];

    if (stat(path, &stat_buf)) {
        printf("Stat failed for %s\n", path);
        return -1;
    }

    if ((stat_buf.st_mode & S_IFMT) == S_IFREG) {
        init_connection(event_loop, addr, path, file_name_from_path(path), stat_buf.st_size);
    } else if ((stat_buf.st_mode & S_IFMT) == S_IFDIR) {
        if (!(dir = opendir(path))) {
            printf("Failed to open dir %s\n", path);
            return -1;
        }
        while ((entry = readdir(dir)) != NULL) {
            if (memcmp(entry->d_name, ".", 1) == 0 || memcmp(entry->d_name, "..", 2) == 0)
                continue;
            path_join(path, entry->d_name, path_buf, BUFSIZ);
            if (stat(path_buf, &stat_buf) || ((stat_buf.st_mode & S_IFMT) != S_IFREG)) {
                printf("Invalid file %s\n", path_buf);
                continue;
            }
            init_connection(event_loop, addr, path_buf, entry->d_name, stat_buf.st_size);
        }
    } else {
        printf("Invalid path %s\n", path);
        return -1;
    }

    return 0;
}

static int has_unfinished_connections()
{
    for (; connections; connections = connections->next) {
        if (connections->status == in_progress)
            return 1;
    }

    return 0;
}

void wait_for_done(void *event_loop)
{
    int ecode;
    void *thread_result;

    pthread_mutex_lock(&mutex);
    while (has_unfinished_connections())
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
    char path_buf[BUFSIZ];
    char addr_buf[BUFSIZ];
    void *event_loop;
    struct connection_ctx *tmp;

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(path_buf, 0, BUFSIZ);
    memset(addr_buf, 0, BUFSIZ);
    mode = MODE_ASYNC;
    while ((opt = getopt(argc, argv, "a:p:m:h")) != -1) {
        switch (opt) {
        case 'p':
            strncpy(path_buf, optarg, BUFSIZ - 1);
            path_buf[BUFSIZ - 1] = '\0';
            break;
        case 'a':
            strncpy(addr_buf, optarg, BUFSIZ - 1);
            addr_buf[BUFSIZ - 1] = '\0';
            break;
        case 'm':
            if (strcmp(optarg, "seq") == 0)
                mode = MODE_SEQ;
            break;
        case 'h':
            printf("Example tcp client. Sends file(s) from <path> to the specified <address>.\n" \
                   " -a <host:port>\n" \
                   " -p <path>  (Might be a single file or a directory)\n"
                   " -m <mode>  {seq | async} (write-read-write mode, default is 'async')\n");
            return EXIT_SUCCESS;
        }
    }

    if (!path_buf[0] || !addr_buf[0]) {
        printf("Required options mising. Run with -h to get help.\n");
        return EXIT_FAILURE;
    }

    event_loop = start_event_loop();
    if (!event_loop) {
        printf("Failed to create and start event loop. Bailing out.\n");
        return EXIT_FAILURE;
    }

    connections = NULL;
    do_work(event_loop,addr_buf, path_buf);
    wait_for_done(event_loop);

    while(connections) {
        tmp = connections;
        connections = connections->next;
        free_connection_ctx(tmp);
    }

    cio_free_event_loop(event_loop);
}
