#if !defined(CIO_ECHO_CLIENT_H)
#define CIO_ECHO_CLIENT_H

void *new_echo_client(void *event_loop, int send_count, const char *addr_string, int port);
void free_echo_client(void *echo_client);
void echo_client_start(void *echo_client);
void echo_client_stop(void *echo_client);
int echo_client_received_count(void *echo_client);

#endif /* CIO_ECHO_CLIENT_H */
