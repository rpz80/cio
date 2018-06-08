#if !defined(CIO_TCP_CLIENT_H)
#define CIO_TCP_CLIENT_H

#include <sys/socket.h>
#include <sys/types.h>

/**
 * fd - previously created socket descriptor. It is added to the event_loop within this
 * call.
 * ctx - user-provided context. It will be passed to the async functions callbacks.
 */
void *cio_new_tcp_client_fd(void *event_loop, void *ctx, int fd);

/**
 * addr - resolvable (to ipV6 or ipv4) address string
 */
void *cio_new_tcp_client_addr(void *event_loop, void *ctx, const char *addr, int port);

/* Socket descriptor is removed from event_loop and closed within this call. */
void cio_free_tcp_client(void *tcp_client);

void cio_tcp_client_async_connect(void *tcp_client, const struct sockaddr *address,
    socklen_t addrlen, void (*on_connect)(void *ctx, int ecode));

void cio_tcp_client_async_read(void *tcp_client, void *data, int len,
    void (*on_read)(void *ctx, int ecode, int read_len));

void cio_tcp_client_async_write(void *tcp_client, const void *data, int len,
    void (*on_write)(void *ctx, int ecode, int written_len));


#endif /* CIO_TCP_CLIENT_H */
