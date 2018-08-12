#if !defined(CIO_TCP_CONNECTION_H)
#define CIO_TCP_CONNECTION_H

/**
 * ctx - user-provided context. It will be passed to the async functions callbacks.
 */
void *cio_new_tcp_connection(void *event_loop, void *ctx);

/**
 * Create tcp_connection with already connected socket (fd).
 */
void *cio_new_tcp_connection_connected_fd(void *event_loop, void *ctx, int fd);

void cio_free_tcp_connection(void *tcp_connection);

void cio_tcp_connection_async_connect(void *tcp_connection, const char *addr, int port,
    void (*on_connect)(void *ctx, int ecode));

void cio_tcp_connection_async_read(void *tcp_connection, void *data, int len,
    void (*on_read)(void *ctx, int ecode, int read_bytes));

void cio_tcp_connection_async_write(void *tcp_connection, const void *data, int len,
    void (*on_write)(void *ctx, int ecode));

#endif /* CIO_TCP_CONNECTION_H */
