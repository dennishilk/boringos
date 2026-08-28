#ifndef BORING_M37_DESKTOP_TEST_H
#define BORING_M37_DESKTOP_TEST_H

#include <boring/boot_protocol.h>

void m37_desktop_test_run(
    const struct boring_limine_module_response *modules)
    __attribute__((noreturn));

#endif
