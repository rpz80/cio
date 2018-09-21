#include "../common.h"
#include <cio_tcp_connection.h>
#include <cio_event_loop.h>
#include <cio_tcp_server.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>

enum connection_state {
    name_len,
    name,
    file_len,
    file
};

struct connection_ctx {
    void *connection;
    enum connection_state state;
    char buf[4096];
    int buf_data_size;
    int buf_data_offset;
    int file_name_len;
    int file_size;
    int transferred;
    char file_name[BUFSIZ];
    int fd;
};

static char path_buf[BUFSIZ];

static void free_connection_ctx(struct connection_ctx *ctx)
{
    if (ctx) {
        if (ctx->connection)
            cio_free_tcp_connection(ctx->connection);
            close(ctx->fd);
        free(ctx);
    }
}

static int setup_data_dir(const char *path)
{
    struct stat stat_buf;
    if (stat(path, &stat_buf) == -1 && mkdir(path, 0777) == -1) {
        perror("setup_data_dir");
        return -1;
    }

    if (stat(path, &stat_buf) == -1) {
        perror("setup_data_dir: stat after mkdir");
        return -1;
    }

    if ((stat_buf.st_mode & S_IFMT) != S_IFDIR) {
        printf("setup_data_dir: '%s' is not a directory\n", path);
        return -1;
    }

    return 0;
}

static void on_read(void *ctx, int ecode, int bytes_read);

static int prepare_file(struct connection_ctx *cctx)
{
    char path[BUFSIZ];

    path_join(path_buf, cctx->file_name, path, BUFSIZ);
    if ((cctx->fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU)) == -1) {
        perror("open");
        return -1;
    }

    return 0;
}

static void on_write(void *ctx, int ecode)
{
    struct connection_ctx *cctx = ctx;

    if (ecode != CIO_NO_ERROR) {
        cio_perror(ecode, "on_write");
        free_connection_ctx(cctx);
        return;
    }

    cio_tcp_connection_async_read(cctx->connection, cctx->buf, sizeof(cctx->buf), on_read);
}

static void parse_buf(struct connection_ctx *cctx)
{
    int written, tmp;

    switch (cctx->state) {
    case name_len:
        if (cctx->buf_data_size < sizeof(cctx->file_name_len)) {
            cio_tcp_connection_async_read(cctx->connection, cctx->buf + cctx->buf_data_size,
                sizeof(cctx->buf) - cctx->buf_data_size, on_read);
        } else {
            cctx->file_name_len = ntohl(*((int*)cctx->buf));
            cctx->buf_data_offset += sizeof(cctx->file_name_len);
            cctx->state = name;
            parse_buf(cctx);
        }
        break;
    case name:
        if (cctx->buf_data_size - cctx->buf_data_offset < cctx->file_name_len) {
            cio_tcp_connection_async_read(cctx->connection, cctx->buf + cctx->buf_data_size,
                sizeof(cctx->buf) - cctx->buf_data_size, on_read);
        } else {
            memcpy(cctx->file_name, cctx->buf + cctx->buf_data_offset, cctx->file_name_len);
            cctx->buf_data_offset += cctx->file_name_len;
            cctx->state = file_len;
            parse_buf(cctx);
        }
        break;
    case file_len:
        if (cctx->buf_data_size - cctx->buf_data_offset < sizeof(cctx->file_size)) {
            cio_tcp_connection_async_read(cctx->connection, cctx->buf + cctx->buf_data_size,
                sizeof(cctx->buf) - cctx->buf_data_size, on_read);
        } else {
            cctx->file_size = ntohl(*((int*)(cctx->buf + cctx->buf_data_offset)));
            cctx->buf_data_offset += sizeof(cctx->file_size);
            cctx->state = file;
            if (prepare_file(cctx)) {
                printf("Failed to prepare file %s\n", cctx->file_name);
                goto fail;
            }
            parse_buf(cctx);
        }
        break;
    case file:
        written = write(cctx->fd, cctx->buf + cctx->buf_data_offset,
            cctx->buf_data_size - cctx->buf_data_offset);
        if (written == -1) {
            perror("write");
            goto fail;
        }
        cctx->transferred += written;
        cctx->buf_data_size = 0;
        cctx->buf_data_offset = 0;
        tmp = htonl(cctx->transferred);
        memcpy(cctx->buf, &tmp, sizeof(tmp));
        cio_tcp_connection_async_write(cctx->connection, &cctx->buf, sizeof(tmp), on_write);
        break;
    }
    return;

fail:
    free_connection_ctx(cctx);
}

static void on_read(void *ctx, int ecode, int bytes_read)
{
    struct connection_ctx *cctx = ctx;

    if (ecode != CIO_NO_ERROR || bytes_read == 0)
        goto fail;

    cctx->buf_data_size += bytes_read;
    parse_buf(cctx);
    return;

fail:
    if (ecode != CIO_NO_ERROR)
        cio_perror(ecode, "on_read");
    else
        printf("on_read: connection closed, file: %s\n", cctx->file_name);

    free_connection_ctx(cctx);
}

static void on_accept(int fd, void *user_ctx, int ecode)
{
    struct connection_ctx *cctx;
    void *connection = NULL;
    void *event_loop = user_ctx;

    if (ecode != CIO_NO_ERROR) {
        cio_perror(ecode, "on_accept");
        return;
    }

    cctx = malloc(sizeof(*cctx));
    if (!cctx) {
        perror("on_accept: malloc");
        goto fail;
    }
    memset(cctx, 0, sizeof(*cctx));

    connection = cio_new_tcp_connection_connected_fd(event_loop, cctx, fd);
    if (!connection) {
        printf("on_accept: cio_new_tcp_connection_connected_fd: failed\n");
        goto fail;
    }

    cctx->connection = connection;
    cio_tcp_connection_async_read(cctx->connection, cctx->buf, sizeof(cctx->buf), on_read);

    return;

fail:
    close(fd);
    free_connection_ctx(cctx);
}

int main(int argc, char *const argv[])
{
    int opt, port;
    char addr_buf_option[BUFSIZ];
    char host_addr[BUFSIZ];
    void *event_loop = NULL;
    void *tcp_server = NULL;
    void *thread_result;

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(path_buf, 0, BUFSIZ);
    memset(addr_buf_option, 0, BUFSIZ);
    while ((opt = getopt(argc, argv, "a:p:h")) != -1) {
        switch (opt) {
        case 'p':
            strncat(path_buf, optarg, BUFSIZ - 1);
            break;
        case 'a':
            strncat(addr_buf_option, optarg, BUFSIZ - 1);
            break;
        case 'h':
            printf("Example tcp server. Receives file(s) and writes them to the <path>.\n" \
                   " -a <host:port>  for example: -a 0.0.0.0:27158 \n" \
                   " -p <path>  absolute path to the files directory (/tmp/cio_example_server_data by default)\n");
            return EXIT_SUCCESS;
        }
    }

    if (!addr_buf_option[0]) {
        printf("Address is required. Run with -h to get help.\n");
        return EXIT_FAILURE;
    }

    if (!path_buf[0])
        strncat(path_buf, "/tmp/cio_example_server_data", BUFSIZ);

    if (setup_data_dir(path_buf))
        return EXIT_FAILURE;

    event_loop = start_event_loop();
    if (!event_loop) {
        printf("Failed to create and start event loop. Bailing out.\n");
        return EXIT_FAILURE;
    }

    tcp_server = cio_new_tcp_acceptor(event_loop, event_loop);
    if (!tcp_server) {
        printf("Failed to create tcp_server instance. Bailing out.\n");
        cio_free_event_loop(event_loop);
        return EXIT_FAILURE;
    }

    if (parse_addr_string(addr_buf_option, host_addr, BUFSIZ, &port)) {
        printf("Invalid address: %s\n", addr_buf_option);
        cio_free_tcp_acceptor(tcp_server);
        cio_free_event_loop(event_loop);
        return EXIT_FAILURE;
    }

    cio_tcp_acceptor_async_accept(tcp_server, host_addr, port, on_accept);
    pthread_join(event_loop_thread, &thread_result);
    cio_free_tcp_acceptor(tcp_server);
    cio_free_event_loop(event_loop);

    return EXIT_SUCCESS;
}
