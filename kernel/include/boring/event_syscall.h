#ifndef BORING_EVENT_SYSCALL_H
#define BORING_EVENT_SYSCALL_H
struct x86_64_syscall_frame;
void x86_64_syscall_dispatch_events(struct x86_64_syscall_frame *frame);
void boring_event_input_irq(void);
#endif
