#if !defined (CIO_INT_HASH_SET_UT_H)
#define CIO_INT_HASH_SET_UT_H

int setup_int_hash_set_tests_with_release(void **ctx);
int teardown_int_hash_set_tests(void **ctx);
void test_int_hash_set_w_release(void **ctx);

int setup_int_hash_set_tests_linked_list(void **ctx);
void test_int_hash_set_linked_list(void **ctx);

#endif // CIO_INT_HASH_SET_UT_H
