#ifndef BORING_M38_DESKTOP_TEST_H
#define BORING_M38_DESKTOP_TEST_H

#include <boring/boot_protocol.h>

void m38_desktop_test_run(
    const struct boring_limine_module_response *modules)
    __attribute__((noreturn));
void m38_desktop_test_finish_from_pid1(void) __attribute__((noreturn));

#endif
