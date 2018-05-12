#if !defined (CIO_HASH_SET_UT_H)
#define CIO_HASH_SET_UT_H

int setup_hash_set_tests_with_release(void **ctx);
int teardown_hash_set_tests(void **ctx);

void test_int_hash_set_w_release(void **ctx);

#endif // CIO_HASH_SET_UT_H
