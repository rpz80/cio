/**
 * Tcp connection abstraction. All operations are run on the provided event loop thread to make it
 * possible not to explicitely synchronize data access in the callbacks. Note that the event loop
 * object MUST outlive the connection object because even the destruction of the connection is made
 * on the event loop thread.
 */

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

/**
 * Asynchronous destruction. For use from within the event loop thread or when you dont't need to
 * wait for the connection to destroy.
 */
void cio_free_tcp_connection_async(void *tcp_connection);

/**
 * Synchronous destruction. For use NOT from the event loop thread. Blocks until connection is
 * destroyed.
 */
void cio_free_tcp_connection_sync(void *tcp_connection);

void cio_tcp_connection_async_connect(void *tcp_connection, const char *addr, int port,
    void (*on_connect)(void *ctx, int ecode));

void cio_tcp_connection_async_read(void *tcp_connection, void *data, int len,
    void (*on_read)(void *ctx, int ecode, int read_bytes));

void cio_tcp_connection_async_write(void *tcp_connection, const void *data, int len,
    void (*on_write)(void *ctx, int ecode));

#endif /* CIO_TCP_CONNECTION_H */
