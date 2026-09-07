#ifndef BORING_IPC_TEST_H
#define BORING_IPC_TEST_H

#include <stdbool.h>

#include <boring/boot_protocol.h>

struct process;

void ipc_test_run(const struct boring_limine_module_response *modules)
    __attribute__((noreturn));
bool boring_ipc_test_process_exit_prepare(struct process *process);

#endif
