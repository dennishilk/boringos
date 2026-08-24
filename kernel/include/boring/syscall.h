#ifndef BORING_SYSCALL_H
#define BORING_SYSCALL_H

#include <boring/syscall_abi.h>

#define X86_64_SYSCALL_STACK_SIZE 16384U

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct x86_64_syscall_frame {
    uint64_t user_rsp;
    uint64_t user_rip;
    uint64_t user_rflags;
    uint64_t syscall_number;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t r10;
    uint64_t r8;
    uint64_t r9;
    uint64_t result;
    uint64_t reserved;
};

struct syscall_stats {
    uint64_t efer;
    uint64_t star;
    uint64_t lstar;
    uint64_t fmask;
    uintptr_t stack_base;
    uintptr_t stack_top;
    uintptr_t last_kernel_rsp;
    uintptr_t last_user_rsp;
    uint64_t dispatch_count;
    bool supported;
    bool initialized;
};

bool syscall_init(void);
bool syscall_get_stats(struct syscall_stats *stats);
bool syscall_stack_contains(uintptr_t stack_pointer);
bool syscall_console_safety_self_test(uintptr_t read_only_user_address,
                                      uintptr_t unmapped_user_address);
void x86_64_syscall_dispatch(struct x86_64_syscall_frame *frame);

#endif

#endif
