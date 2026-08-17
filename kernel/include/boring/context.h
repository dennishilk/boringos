#ifndef BORING_CONTEXT_H
#define BORING_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

struct x86_64_kernel_context {
    uint64_t rsp;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

void x86_64_context_switch(struct x86_64_kernel_context *from,
                           const struct x86_64_kernel_context *to);

bool x86_64_context_test_callee_saved(void (*yield_fn)(void));
void x86_64_preemption_probe_reset(void);
void x86_64_preemption_probe_release(void);
bool x86_64_context_test_preemptive_gprs(void);

#endif
