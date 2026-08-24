#include <stddef.h>
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

void serial_write_bytes(const char *data, size_t length) {
    size_t index;

    if (data == NULL) {
        return;
    }

    for (index = 0U; index < length; ++index) {
        serial_write_char(data[index]);
    }
}

void serial_write_string(const char *text) {
    while (*text != '\0') {
        serial_write_char(*text);
        ++text;
    }
}

char serial_read_char_blocking(void) {
    while ((x86_64_in8((uint16_t)COM1_LINE_STATUS) & 0x01u) == 0u) {
    }

    return (char)x86_64_in8((uint16_t)COM1_DATA);
}

void serial_write_u64(uint64_t value) {
    char digits[20];
    size_t count = 0U;

    if (value == 0ULL) {
        serial_write_char('0');
        return;
    }

    while (value != 0ULL) {
        const uint8_t digit = (uint8_t)(value % 10ULL);
        digits[count] = (char)((uint8_t)'0' + digit);
        ++count;
        value /= 10ULL;
    }

    while (count != 0U) {
        --count;
        serial_write_char(digits[count]);
    }
}

void serial_write_hex_u64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    unsigned int nibble_index;

    serial_write_string("0x");
    for (nibble_index = 0U; nibble_index < 16U; ++nibble_index) {
        const unsigned int shift = (15U - nibble_index) * 4U;
        const uint8_t digit = (uint8_t)((value >> shift) & 0x0fULL);
        serial_write_char(digits[digit]);
    }
}
