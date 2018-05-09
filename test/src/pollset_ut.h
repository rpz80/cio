#if !defined(CIO_POLLSET_UT_H)
#define CIO_POLLSET_UT_H

void test_pollset_new(void **ctx);

int setup_pollset_tests(void **ctx);
int teardown_pollset_tests(void **ctx);

void test_pollset_add(void **ctx);
void test_pollset_remove(void **ctx);
void test_pollset_poll(void **ctx);

#endif // POLLSET_UT_H
