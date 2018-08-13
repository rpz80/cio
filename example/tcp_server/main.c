#include <tcp_connection.h>
#include <fcntl.h>

int main(int argc, char *const argv[])
{
    int opt;
    char path_buf[BUF_SIZE];
    char addr_buf[BUF_SIZE];
    void *event_loop;

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
                   " -a <host:port>\n" \
                   " -p <path>  absolute path to the files directory (./data by default)\n");
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
    do_work(event_loop, &connections, addr_buf, path_buf);
    wait_for_done(event_loop, connections);

    while(connections) {
        tmp = connections;
        connections = connections->next;
        free_connection_ctx(tmp);
    }

    cio_free_event_loop(event_loop);
}
