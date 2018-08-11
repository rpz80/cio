#include <tcp_connection.h>
#include <event_loop.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BUF_SIZE 1024

static int do_work(void *event_loop, const char *addr, const char *path)
{
    DIR *dir;
    struct dirent *entry;
    void **connections;
    const char **files;
    struct stat stat_buf;

    if (stat(path, &stat_buf)) {
        perror("stat");
        return -1;
    }

    if ((stat_buf.st_mode & S_IFMT) == S_IFREG) {

    }

    void *connection = cio_new_tcp_connection(event_loop, NULL);

}

static pthread_t event_loop_thread;

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
        return NULL;
    }

    return event_loop;
}

static void stop_event_loop(void *event_loop)
{
    int ecode;
    void *thread_result;

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

    do_work(event_loop, addr_buf, path_buf);
    stop_event_loop(event_loop);
    cio_free_event_loop(event_loop);
}
