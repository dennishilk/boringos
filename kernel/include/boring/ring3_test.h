#ifndef BORING_RING3_TEST_H
#define BORING_RING3_TEST_H

#include <stdbool.h>

struct x86_64_trap_frame;

bool ring3_test_exception_armed(void);
void ring3_test_run(void) __attribute__((noreturn));
void ring3_test_handle_exception(const struct x86_64_trap_frame *frame)
    __attribute__((noreturn));

#endif
