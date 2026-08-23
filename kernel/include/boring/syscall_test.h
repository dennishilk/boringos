#ifndef BORING_SYSCALL_TEST_H
#define BORING_SYSCALL_TEST_H

#include <stdbool.h>

struct x86_64_trap_frame;

bool syscall_test_exception_armed(void);
void syscall_test_run(void) __attribute__((noreturn));
void syscall_test_handle_exception(const struct x86_64_trap_frame *frame)
    __attribute__((noreturn));

#endif
