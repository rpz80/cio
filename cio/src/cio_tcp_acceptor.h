#if !defined(CIO_TCP_ACCEPTOR_H)
#define CIO_TCP_ACCEPTOR_H

void *cio_new_tcp_acceptor(void *event_loop, void *user_ctx);

void cio_free_tcp_acceptor(void *tcp_server);

void cio_tcp_acceptor_async_accept(void *tcp_server, const char *addr, int port,
    void (*on_accept)(int fd, void *user_ctx, int ecode));

#endif /* CIO_TCP_ACCEPTOR_H */
