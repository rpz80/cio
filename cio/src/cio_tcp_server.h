#if !defined(CIO_TCP_SERVER_H)
#define CIO_TCP_SERVER_H

void *cio_new_tcp_server(void *event_loop, void *user_ctx,
    void (*on_accept)(void *connection, void *user_ctx, int ecode));

void cio_free_tcp_server(void *tcp_server);

void cio_tcp_server_serve(void *tcp_server, const char *addr, int port);

#endif /* CIO_TCP_SERVER_H */
