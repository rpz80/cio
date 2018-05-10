#include "pollset_ut.h"
#include "event_loop_ut.h"
#include "hash_set_ut.h"
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
        TEST(test_event_loop_add_fd)
    };
    struct ct_ut hash_set_tests[] = {
        TEST(test_hash_set_add)
    };
    result = RUN_TESTS(pollset_tests, setup_pollset_tests, teardown_pollset_tests);
    result |= RUN_TESTS(event_loop_tests, setup_event_loop_tests, teardown_event_loop_tests);
    result |= RUN_TESTS(hash_set_tests, setup_hash_set_tests_with_release, teardown_hash_set_tests);
    return result;
}
