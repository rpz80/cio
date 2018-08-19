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


static void on_write(void *ctx, int ecode);
static void send_file(struct connection_ctx *ctx, int send_header);

static void on_read(void *ctx, int ecode, int bytes_read)
{
    struct connection_ctx *cctx = ctx;

    printf("on_read\n");
    if (ecode != CIO_NO_ERROR) {
        cio_perror(ecode, "on_read");
        goto fail;
    }

    if (bytes_read == 0) {
        printf("Connection closed by peer, file: %s\n", cctx->file_name);
        goto fail;
    }

    if (bytes_read != 4) {
        printf("on_read: received message of unexpected len for file %s, closing\n", cctx->file_name);
        goto fail;
    }

    cctx->transferred += ntohl(*(int*)(cctx->buf));
    printf("File %s: %d%%\n", cctx->file_name, (int) (((double) cctx->transferred / cctx->size) * 100));

    if (cctx->transferred == cctx->size) {
        connection_ctx_set_status(ctx, done);
        printf("File %s transferred successfully\n", cctx->file_name);
        return;
    }

    send_file(ctx, 0);
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

static void send_file(struct connection_ctx *ctx, int send_header)
{
    int offset = 0;
    int tmp;
    int file_bytes_read, file_name_len;

    if (send_header) {
        file_name_len = strlen(ctx->file_name);
        tmp = htonl(file_name_len);
        memcpy(ctx->buf, &tmp, sizeof(tmp));
        offset += sizeof(tmp);
        tmp = MIN(file_name_len, sizeof(ctx->buf) - offset);
        memcpy(ctx->buf + offset, ctx->file_name, tmp);
        offset += tmp;
        tmp = htonl(ctx->size);
        memcpy(ctx->buf + offset, &tmp, sizeof(tmp));
        offset += sizeof(tmp);
    }

    file_bytes_read = read(ctx->fd, ctx->buf + offset, sizeof(ctx->buf) - offset);
    if (file_bytes_read == -1) {
        printf("Error reading file %s\n", ctx->file_name);
        connection_ctx_set_status(ctx, failed);
        return;
    }

    offset += file_bytes_read;
    cio_tcp_connection_async_write(ctx->connection, ctx->buf, offset, on_write);
}

static void on_connect(void *ctx, int ecode)
{
    struct connection_ctx *connection_ctx = ctx;

    printf("on_connect\n");
    if (ecode != CIO_NO_ERROR) {
        cio_perror((enum CIO_ERROR) ecode, "Connection failed");
        connection_ctx_set_status(connection_ctx, failed);
        return;
    }

    send_file(ctx, 1);
}

static void init_connection(void *event_loop, const char *addr, const char *path, size_t size)
{
    struct connection_ctx *ctx;
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

    add_connection(ctx);
    cio_tcp_connection_async_connect(ctx->connection, buf, port, on_connect);
}

static int do_work(void *event_loop, const char *addr, const char *path)
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
        init_connection(event_loop, addr, path, stat_buf.st_size);
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
            init_connection(event_loop, addr, path_buf, stat_buf.st_size);
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
    char path_buf[BUF_SIZE];
    char addr_buf[BUF_SIZE];
    void *event_loop;
    struct connection_ctx *tmp;

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

    if (!path_buf[0] || !addr_buf[0]) {
        printf("Required options are mising. Run with -h to get help.\n");
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
