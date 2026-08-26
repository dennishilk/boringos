#ifndef BORING_DISPLAY_TEST_H
#define BORING_DISPLAY_TEST_H

#include <stdbool.h>

struct boring_limine_module_response;
struct process;

void display_test_run(const struct boring_limine_module_response *modules)
    __attribute__((noreturn));
bool boring_display_test_process_exit_prepare(struct process *process);

#endif
