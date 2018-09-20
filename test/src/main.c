#include "pollset_ut.h"
#include "event_loop_ut.h"
#include "int_hash_set_ut.h"
#include "struct_hash_set_ut.h"
#include "tcp_connection_ut.h"
#include <ct.h>

int main(int argc, char *argv[])
{
    int result;

    ct_initialize(argc, argv);
    struct ct_ut pollset_tests[] = {
        TEST(test_pollset_new),
        TEST(test_pollset_add),
        TEST(test_pollset_remove),
        TEST(test_pollset_poll)
    };

    struct ct_ut event_loop_tests[] = {
        TEST(test_event_loop_add_remove),
        TEST(test_event_loop_add_remove_from_cb),
        TEST(test_event_loop_timers),
    };

    struct ct_ut hash_set_tests[] = {
        TEST_SETUP_TEARDOWN(test_int_hash_set_w_release, setup_int_hash_set_tests_with_release,
            teardown_int_hash_set_tests),
        TEST_SETUP_TEARDOWN(test_struct_hash_set_without_release,
            setup_struct_hash_set_tests_without_release, teardown_struct_hash_set_tests),
        TEST_SETUP_TEARDOWN(test_int_hash_set_linked_list,
            setup_int_hash_set_tests_linked_list, teardown_int_hash_set_tests)
    };
    
    struct ct_ut tcp_connection_tests[] = {
        TEST(test_new_tcp_connection)
    };

    result = RUN_TESTS(pollset_tests, setup_pollset_tests, teardown_pollset_tests);
    result |= RUN_TESTS(event_loop_tests, setup_event_loop_tests, teardown_event_loop_tests);
    result |= RUN_TESTS(hash_set_tests, NULL, NULL);
    result |= RUN_TESTS(tcp_connection_tests, setup_tcp_connnection_tests,
                        teardown_tcp_connnection_tests);

    return result;
}
