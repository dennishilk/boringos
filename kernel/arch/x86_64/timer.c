#include <stdbool.h>
#include <stdint.h>

#include <boring/io.h>
#include <boring/irq.h>
#include <boring/timer.h>

enum {
    PIT_CHANNEL0_DATA = 0x40,
    PIT_COMMAND = 0x43,
    PIT_CHANNEL0_LOHI_MODE2_BINARY = 0x34
};

static volatile uint64_t timer_tick_count;
static struct timer_stats timer_state;
static bool timer_initialized;

bool timer_init(uint32_t frequency_hz) {
    uint64_t rounded_divisor;
    uint64_t effective_millihz;
    uint16_t divisor;

    timer_initialized = false;
    timer_tick_count = 0ULL;
    timer_state.input_frequency_hz = X86_64_PIT_INPUT_FREQUENCY_HZ;
    timer_state.requested_frequency_hz = 0U;
    timer_state.divisor = 0U;
    timer_state.effective_frequency_millihz = 0U;

    if ((frequency_hz == 0U) ||
        (frequency_hz > X86_64_PIT_INPUT_FREQUENCY_HZ)) {
        return false;
    }

    rounded_divisor =
        ((uint64_t)X86_64_PIT_INPUT_FREQUENCY_HZ +
         ((uint64_t)frequency_hz / 2ULL)) /
        (uint64_t)frequency_hz;
    if ((rounded_divisor == 0ULL) || (rounded_divisor > 65535ULL)) {
        return false;
    }
    divisor = (uint16_t)rounded_divisor;

    effective_millihz =
        (((uint64_t)X86_64_PIT_INPUT_FREQUENCY_HZ * 1000ULL) +
         ((uint64_t)divisor / 2ULL)) /
        (uint64_t)divisor;
    if (effective_millihz > (uint64_t)UINT32_MAX) {
        return false;
    }

    x86_64_out8((uint16_t)PIT_COMMAND,
                 (uint8_t)PIT_CHANNEL0_LOHI_MODE2_BINARY);
    x86_64_out8((uint16_t)PIT_CHANNEL0_DATA,
                 (uint8_t)((uint32_t)divisor & 0xffU));
    x86_64_out8((uint16_t)PIT_CHANNEL0_DATA,
                 (uint8_t)(((uint32_t)divisor >> 8) & 0xffU));

    timer_state.requested_frequency_hz = frequency_hz;
    timer_state.divisor = divisor;
    timer_state.effective_frequency_millihz = (uint32_t)effective_millihz;
    timer_initialized = true;

    if (!irq_unmask_timer()) {
        timer_initialized = false;
        return false;
    }

    return true;
}

bool timer_get_stats(struct timer_stats *stats) {
    if ((!timer_initialized) || (stats == 0)) {
        return false;
    }

    *stats = timer_state;
    return true;
}

uint64_t timer_ticks(void) {
    return timer_tick_count;
}

bool timer_handle_irq(void) {
    if (!timer_initialized) {
        return false;
    }

    ++timer_tick_count;
    return true;
}
