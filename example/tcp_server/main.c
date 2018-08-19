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

static void on_accept(void *connection, void *user_ctx, int ecode)
{
    if (ecode != CIO_NO_ERROR) {
        cio_perror(ecode, "on_accept");
        return;
    }


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
