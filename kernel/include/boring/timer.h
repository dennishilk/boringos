#ifndef BORING_TIMER_H
#define BORING_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define X86_64_PIT_INPUT_FREQUENCY_HZ 1193182U

struct timer_stats {
    uint32_t input_frequency_hz;
    uint32_t requested_frequency_hz;
    uint16_t divisor;
    uint32_t effective_frequency_millihz;
};

bool timer_init(uint32_t frequency_hz);
bool timer_get_stats(struct timer_stats *stats);
uint64_t timer_ticks(void);
bool timer_handle_irq(void);

#endif
