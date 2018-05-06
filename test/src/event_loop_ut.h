#if !defined(EVENT_LOOP_UT_H)
#define EVENT_LOOP_UT_H

int setup_event_loop_tests(void **ctx);
int teardown_event_loop_tests(void **ctx);

void test_event_loop_add_fd(void **ctx);

#endif // EVENT_LOOP_UT_H
