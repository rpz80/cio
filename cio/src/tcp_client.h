#if !defined(CIO_TCP_CLIENT_H)
#define CIO_TCP_CLIENT_H

#include <sys/socket.h>
#include <sys/types.h>

/**
 * address, addrlen - might be acquired with cio_resolver (cio/resolv.h).
 * ctx - user-provided context. It will be passed to the async functions callbacks.
 */
void *cio_new_tcp_client(void *event_loop, void *ctx);

/* Socket descriptor is removed from event_loop and closed within this call. */
void cio_free_tcp_client(void *tcp_client);

void cio_tcp_client_async_connect(void *tcp_client, const char *addr, int port,
    void (*on_connect)(void *ctx, int ecode));

void cio_tcp_client_async_read(void *tcp_client, void *data, int len,
    void (*on_read)(void *ctx, int ecode, int read_len));

void cio_tcp_client_async_write(void *tcp_client, const void *data, int len,
    void (*on_write)(void *ctx, int ecode, int written_len));

#endif /* CIO_TCP_CLIENT_H */
