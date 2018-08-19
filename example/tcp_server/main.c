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
    char file_name[BUF_SIZE];
};

static void free_connection_ctx(struct connection_ctx *ctx)
{
    cio_free_tcp_connection(ctx->connection);
    free(ctx);
}

static int setup_data_dir(const char *path)
{
    struct stat stat_buf;
    if (stat(path, &stat_buf) == -1 && mkdir(path, 0777) == -1) {
        perror("setup_data_dir");
        return -1;
    }

    if ((stat_buf.st_mode & S_IFMT) != S_IFDIR) {
        printf("setup_data_dir: '%s' is not a directory\n", path);
        return -1;
    }

    return 0;
}

static void on_read(void *ctx, int ecode, int bytes_read)
{
    struct connection_ctx *cctx = ctx;
    int offset = 0;
    int nlen;

    if (ecode != CIO_NO_ERROR || bytes_read == 0)
        goto fail;

    switch (cctx->state) {
    case name_len:

        break;
    }

fail:
    if (ecode != CIO_NO_ERROR)
        cio_perror(ecode, "on_read");
    else
        printf("on_read: connection closed, file: %s\n", cctx->file_name);

    cio_free_tcp_connection(cctx->connection);
    free(cctx);
}

static void on_accept(void *connection, void *user_ctx, int ecode)
{
    struct connection_ctx *cctx;

    if (ecode != CIO_NO_ERROR) {struct connection_ctx *cctx;
        cio_perror(ecode, "on_accept");
        return;
    }

    cctx = malloc(sizeof(*cctx));
    if (!cctx) {
        perror("on_accept: malloc");
        return;
    }
    memset(cctx, 0, sizeof(*cctx));
    cctx->connection = connection;

    cio_tcp_connection_async_read(cctx->connection, cctx->buf, sizeof(cctx->buf), on_read);
}

int main(int argc, char *const argv[])
{
    int opt;
    char path_buf[BUF_SIZE];
    char addr_buf[BUF_SIZE];
    void *event_loop = NULL;
    void *tcp_server = NULL;

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
            printf("Example tcp server. Receives file(s) and writes them to the <path>.\n" \
                   " -a <host:port>  for example: -a 0.0.0.0:27158 \n" \
                   " -p <path>  absolute path to the files directory (./data by default)\n");
            return EXIT_SUCCESS;
        }
    }

    if (!addr_buf[0]) {
        printf("Address is required. Run with -h to get help.\n");
        return EXIT_FAILURE;
    }

    if (setup_data_dir(path_buf))
        return EXIT_FAILURE;

    event_loop = start_event_loop();
    if (!event_loop) {
        printf("Failed to create and start event loop. Bailing out.\n");
        return EXIT_FAILURE;
    }

    tcp_server = cio_new_tcp_server(event_loop, NULL, on_accept);
    if (!tcp_server) {
        printf("Failed to create tcp_server instance. Bailing out.\n");
        cio_free_event_loop(event_loop);
        return EXIT_FAILURE;
    }

    cio_free_event_loop(event_loop);

    return EXIT_SUCCESS;
}
