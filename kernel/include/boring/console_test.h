#ifndef BORING_CONSOLE_TEST_H
#define BORING_CONSOLE_TEST_H

#include <stdbool.h>

struct boring_limine_module_response;
struct x86_64_trap_frame;

bool console_test_exception_armed(void);
void console_test_run(const struct boring_limine_module_response *modules)
    __attribute__((noreturn));
void console_test_handle_exception(const struct x86_64_trap_frame *frame)
    __attribute__((noreturn));

#endif
