#include "pollset_ut.h"
#include <ct.h>

int main(int argc, char *argv[])
{
    ct_initialize(argc, argv);
    struct ct_ut pollset_tests[] = {
        TEST(test_pollset_new),
        TEST(test_pollset_add),
        TEST(test_pollset_remove)
    };
    RUN_TESTS(pollset_tests, setup_pollset_tests, teardown_pollset_tests);
}
