#if !defined(CIO_TCP_ACCEPTOR_H)
#define CIO_TCP_ACCEPTOR_H

void *cio_new_tcp_acceptor(void *event_loop, void *user_ctx);

/**
 * Asynchronous destruction. For use from within the event loop thread or when you dont't need to
 * wait for the acceptor to destroy.
 */
void cio_free_tcp_acceptor_async(void *tcp_server);

/**
 * Synchronous destruction. For use NOT from the event loop thread. Blocks until acceptor is
 * destroyed.
 */
void cio_free_tcp_acceptor_sync(void *tcp_server);

void cio_tcp_acceptor_async_accept(void *tcp_server, const char *addr, int port,
    void (*on_accept)(int fd, void *user_ctx, int ecode));

#endif /* CIO_TCP_ACCEPTOR_H */
