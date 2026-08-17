#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/exception.h>
#include <boring/io.h>
#include <boring/irq.h>
#include <boring/timer.h>

enum {
    PIC_MASTER_COMMAND = 0x20,
    PIC_MASTER_DATA = 0x21,
    PIC_SLAVE_COMMAND = 0xa0,
    PIC_SLAVE_DATA = 0xa1,
    PIC_IO_WAIT_PORT = 0x80,
    PIC_ICW1_INIT_ICW4 = 0x11,
    PIC_ICW4_8086 = 0x01,
    PIC_MASTER_HAS_SLAVE_IRQ2 = 0x04,
    PIC_SLAVE_IDENTITY_IRQ2 = 0x02,
    PIC_EOI = 0x20,
    PIC_READ_ISR = 0x0b,
    PIC_ALL_MASKED = 0xff,
    PIC_TIMER_ONLY_MASK = 0xfe
};

extern const uintptr_t x86_64_irq_stub_table[X86_64_PIC_IRQ_COUNT];

static bool irq_initialized;
static bool timer_unmasked;
static uint8_t master_mask = (uint8_t)PIC_ALL_MASKED;
static uint8_t slave_mask = (uint8_t)PIC_ALL_MASKED;
static volatile uint64_t timer_irq_count;
static volatile uint64_t unexpected_irq_count;
static volatile uint64_t spurious_irq7_count;
static volatile uint64_t spurious_irq15_count;

static void pic_io_wait(void) {
    x86_64_out8((uint16_t)PIC_IO_WAIT_PORT, 0U);
}

static uint8_t pic_read_isr(uint16_t command_port) {
    x86_64_out8(command_port, (uint8_t)PIC_READ_ISR);
    return x86_64_in8(command_port);
}

static void pic_send_eoi(uint8_t irq_number) {
    if (irq_number >= 8U) {
        x86_64_out8((uint16_t)PIC_SLAVE_COMMAND, (uint8_t)PIC_EOI);
    }
    x86_64_out8((uint16_t)PIC_MASTER_COMMAND, (uint8_t)PIC_EOI);
}

static bool pic_initialize(void) {
    x86_64_out8((uint16_t)PIC_MASTER_DATA, (uint8_t)PIC_ALL_MASKED);
    x86_64_out8((uint16_t)PIC_SLAVE_DATA, (uint8_t)PIC_ALL_MASKED);

    x86_64_out8((uint16_t)PIC_MASTER_COMMAND,
                 (uint8_t)PIC_ICW1_INIT_ICW4);
    pic_io_wait();
    x86_64_out8((uint16_t)PIC_SLAVE_COMMAND,
                 (uint8_t)PIC_ICW1_INIT_ICW4);
    pic_io_wait();

    x86_64_out8((uint16_t)PIC_MASTER_DATA,
                 (uint8_t)X86_64_PIC_MASTER_VECTOR_BASE);
    pic_io_wait();
    x86_64_out8((uint16_t)PIC_SLAVE_DATA,
                 (uint8_t)X86_64_PIC_SLAVE_VECTOR_BASE);
    pic_io_wait();

    x86_64_out8((uint16_t)PIC_MASTER_DATA,
                 (uint8_t)PIC_MASTER_HAS_SLAVE_IRQ2);
    pic_io_wait();
    x86_64_out8((uint16_t)PIC_SLAVE_DATA,
                 (uint8_t)PIC_SLAVE_IDENTITY_IRQ2);
    pic_io_wait();

    x86_64_out8((uint16_t)PIC_MASTER_DATA, (uint8_t)PIC_ICW4_8086);
    pic_io_wait();
    x86_64_out8((uint16_t)PIC_SLAVE_DATA, (uint8_t)PIC_ICW4_8086);
    pic_io_wait();

    master_mask = (uint8_t)PIC_ALL_MASKED;
    slave_mask = (uint8_t)PIC_ALL_MASKED;
    x86_64_out8((uint16_t)PIC_MASTER_DATA, master_mask);
    x86_64_out8((uint16_t)PIC_SLAVE_DATA, slave_mask);

    return (x86_64_in8((uint16_t)PIC_MASTER_DATA) == master_mask) &&
           (x86_64_in8((uint16_t)PIC_SLAVE_DATA) == slave_mask);
}

bool irq_init(void) {
    size_t index;

    x86_64_interrupts_disable();
    if (x86_64_interrupts_enabled()) {
        return false;
    }

    irq_initialized = false;
    timer_unmasked = false;
    master_mask = (uint8_t)PIC_ALL_MASKED;
    slave_mask = (uint8_t)PIC_ALL_MASKED;
    timer_irq_count = 0ULL;
    unexpected_irq_count = 0ULL;
    spurious_irq7_count = 0ULL;
    spurious_irq15_count = 0ULL;

    for (index = 0U; index < (size_t)X86_64_PIC_IRQ_COUNT; ++index) {
        const uint8_t vector =
            (uint8_t)((uint8_t)X86_64_PIC_MASTER_VECTOR_BASE +
                      (uint8_t)index);
        if ((x86_64_irq_stub_table[index] == (uintptr_t)0U) ||
            !exception_install_interrupt_gate(
                vector, x86_64_irq_stub_table[index])) {
            return false;
        }
    }

    if (!pic_initialize()) {
        return false;
    }

    irq_initialized = true;
    return true;
}

bool irq_unmask_timer(void) {
    if ((!irq_initialized) || x86_64_interrupts_enabled()) {
        return false;
    }

    master_mask = (uint8_t)PIC_TIMER_ONLY_MASK;
    slave_mask = (uint8_t)PIC_ALL_MASKED;
    x86_64_out8((uint16_t)PIC_MASTER_DATA, master_mask);
    x86_64_out8((uint16_t)PIC_SLAVE_DATA, slave_mask);

    if ((x86_64_in8((uint16_t)PIC_MASTER_DATA) != master_mask) ||
        (x86_64_in8((uint16_t)PIC_SLAVE_DATA) != slave_mask)) {
        master_mask = (uint8_t)PIC_ALL_MASKED;
        slave_mask = (uint8_t)PIC_ALL_MASKED;
        x86_64_out8((uint16_t)PIC_MASTER_DATA, master_mask);
        x86_64_out8((uint16_t)PIC_SLAVE_DATA, slave_mask);
        return false;
    }

    timer_unmasked = true;
    return true;
}

bool irq_enable(void) {
    if ((!irq_initialized) || (!timer_unmasked) ||
        (master_mask != (uint8_t)PIC_TIMER_ONLY_MASK) ||
        (slave_mask != (uint8_t)PIC_ALL_MASKED)) {
        return false;
    }

    x86_64_interrupts_enable();
    return x86_64_interrupts_enabled();
}

bool irq_get_stats(struct irq_stats *stats) {
    if ((!irq_initialized) || (stats == NULL)) {
        return false;
    }

    stats->master_mask = master_mask;
    stats->slave_mask = slave_mask;
    stats->timer_irq_count = timer_irq_count;
    stats->unexpected_irq_count = unexpected_irq_count;
    stats->spurious_irq7_count = spurious_irq7_count;
    stats->spurious_irq15_count = spurious_irq15_count;
    return true;
}

void x86_64_irq_dispatch(const struct x86_64_trap_frame *frame) {
    uint8_t irq_number;
    bool expected;

    if ((frame == NULL) ||
        (frame->vector < (uint64_t)X86_64_PIC_MASTER_VECTOR_BASE) ||
        (frame->vector >=
         ((uint64_t)X86_64_PIC_MASTER_VECTOR_BASE +
          (uint64_t)X86_64_PIC_IRQ_COUNT))) {
        ++unexpected_irq_count;
        return;
    }

    irq_number =
        (uint8_t)(frame->vector - (uint64_t)X86_64_PIC_MASTER_VECTOR_BASE);

    if (irq_number == 7U) {
        if ((pic_read_isr((uint16_t)PIC_MASTER_COMMAND) & 0x80U) == 0U) {
            ++spurious_irq7_count;
            return;
        }
    } else if (irq_number == 15U) {
        if ((pic_read_isr((uint16_t)PIC_SLAVE_COMMAND) & 0x80U) == 0U) {
            ++spurious_irq15_count;
            x86_64_out8((uint16_t)PIC_MASTER_COMMAND, (uint8_t)PIC_EOI);
            return;
        }
    }

    if (irq_number < 8U) {
        expected = (master_mask & (uint8_t)(1U << irq_number)) == 0U;
    } else {
        expected = (slave_mask &
                    (uint8_t)(1U << (uint8_t)(irq_number - 8U))) == 0U;
    }

    if ((irq_number == (uint8_t)X86_64_TIMER_IRQ) && expected &&
        timer_handle_irq()) {
        ++timer_irq_count;
    } else {
        ++unexpected_irq_count;
    }

    pic_send_eoi(irq_number);
}
