#include <stdint.h>

#include <boring/io.h>
#include <boring/serial.h>

enum {
    COM1_DATA = 0x3f8,
    COM1_INTERRUPT_ENABLE = 0x3f9,
    COM1_FIFO_CONTROL = 0x3fa,
    COM1_LINE_CONTROL = 0x3fb,
    COM1_MODEM_CONTROL = 0x3fc,
    COM1_LINE_STATUS = 0x3fd
};

static void serial_write_char(char value) {
    while ((x86_64_in8((uint16_t)COM1_LINE_STATUS) & 0x20u) == 0u) {
    }

    x86_64_out8((uint16_t)COM1_DATA, (uint8_t)value);
}

void serial_init(void) {
    x86_64_out8((uint16_t)COM1_INTERRUPT_ENABLE, 0x00u);
    x86_64_out8((uint16_t)COM1_LINE_CONTROL, 0x80u);
    x86_64_out8((uint16_t)COM1_DATA, 0x01u);
    x86_64_out8((uint16_t)COM1_INTERRUPT_ENABLE, 0x00u);
    x86_64_out8((uint16_t)COM1_LINE_CONTROL, 0x03u);
    x86_64_out8((uint16_t)COM1_FIFO_CONTROL, 0xc7u);
    x86_64_out8((uint16_t)COM1_MODEM_CONTROL, 0x0bu);
}

void serial_write_string(const char *text) {
    while (*text != '\0') {
        serial_write_char(*text);
        ++text;
    }
}
