#ifndef BORING_M36_SPAWN_TEST_H
#define BORING_M36_SPAWN_TEST_H

struct boring_limine_module_response;

void m36_spawn_test_run(const struct boring_limine_module_response *modules)
    __attribute__((noreturn));

#endif
