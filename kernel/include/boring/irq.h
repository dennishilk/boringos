#ifndef BORING_IRQ_H
#define BORING_IRQ_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/exception.h>

#define X86_64_PIC_IRQ_COUNT 16U
#define X86_64_PIC_MASTER_VECTOR_BASE 32U
#define X86_64_PIC_SLAVE_VECTOR_BASE 40U
#define X86_64_TIMER_IRQ 0U
#define X86_64_KEYBOARD_IRQ 1U
#define X86_64_MOUSE_IRQ 12U
#define X86_64_TIMER_VECTOR 32U

struct irq_stats {
    uint8_t master_mask;
    uint8_t slave_mask;
    uint64_t timer_irq_count;
    uint64_t keyboard_irq_count;
    uint64_t mouse_irq_count;
    uint64_t unexpected_irq_count;
    uint64_t spurious_irq7_count;
    uint64_t spurious_irq15_count;
};

bool irq_init(void);
bool irq_unmask_timer(void);
bool irq_unmask_input(bool keyboard_online, bool mouse_online);
bool irq_enable(void);
bool irq_get_stats(struct irq_stats *stats);
struct x86_64_trap_frame *x86_64_irq_dispatch(
    struct x86_64_trap_frame *frame);

#endif
