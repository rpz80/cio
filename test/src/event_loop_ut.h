#if !defined(CIO_EVENT_LOOP_UT_H)
#define CIO_EVENT_LOOP_UT_H

int setup_event_loop_tests(void **ctx);
int teardown_event_loop_tests(void **ctx);

void test_event_loop_add_remove(void **ctx);
void test_event_loop_add_remove_from_cb(void **ctx);
void test_event_loop_timers(void **ctx);

#endif /* CIO_EVENT_LOOP_UT_H */
