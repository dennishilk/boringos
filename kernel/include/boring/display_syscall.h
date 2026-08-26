#ifndef BORING_DISPLAY_SYSCALL_H
#define BORING_DISPLAY_SYSCALL_H

struct x86_64_syscall_frame;

void x86_64_syscall_dispatch_m34(struct x86_64_syscall_frame *frame);

#endif
