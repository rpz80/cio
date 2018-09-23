#if !defined(CIO_TCP_CONNECTION_UT_H)
#define CIO_TCP_CONNECTION_UT_H

/**
 * TESTS:
 *   - connect correct address ipv4 +
 *   - connect correct address ipv6
 *   - connect correct address ipv4 && ipv6
 *   - connect invalid address
 *   - connect timeout
 *   - read/write duplex success
 *   - read/write duplex success multiple connections
 *   - read/write sequence success
 *   - read failed
 *   - read timeout
 *   - write success
 *   - write failed
 *   - write timeout
 */

int setup_tcp_connnection_tests(void **ctx);
int teardown_tcp_connnection_tests(void **ctx);

void test_new_tcp_connection(void **ctx);
void test_tcp_connection_connect_correct_address(void **ctx);
void test_tcp_connection_read_write_duplex_success(void **ctx);

#endif //CIO_TCP_SERVER_CLIENT_UT_H
