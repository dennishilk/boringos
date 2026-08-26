#ifndef BORING_IPC_SYSCALL_H
#define BORING_IPC_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

struct process;
struct x86_64_syscall_frame;

extern uintptr_t x86_64_syscall_active_stack_top;

void boring_ipc_syscall_use_task_stack(uintptr_t stack_top);
void boring_ipc_syscall_use_bootstrap_stack(void);
void x86_64_syscall_dispatch_m33(struct x86_64_syscall_frame *frame);

/* Test-harness hook for task-owned Ring-3 process image teardown on SYS_EXIT. */
bool boring_ipc_test_process_exit_prepare(struct process *process);

#endif
