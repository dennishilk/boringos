#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <boring/serial.h>

enum {
    COM1_DATA = 0x3f8,
    COM1_LINE_CONTROL = 0x3fb,
    COM1_MODEM_CONTROL = 0x3fc,
    COM1_LINE_STATUS = 0x3fd,
    COM1_LINE_STATUS_TX_READY = 0x20,
    SERIAL_MODEM_LOOPBACK = 0x1e
};

enum fake_uart_mode {
    FAKE_UART_READY,
    FAKE_UART_ABSENT,
    FAKE_UART_TX_STUCK
};

static enum fake_uart_mode fake_mode;
static bool fake_loopback;
static bool fake_dlab;
static uint8_t fake_loopback_byte;
static size_t fake_line_status_reads;
static size_t fake_data_writes;

uint8_t boring_serial_test_in8(uint16_t port);
void boring_serial_test_out8(uint16_t port, uint8_t value);

static void fail(const char *message) {
    fprintf(stderr, "M61 serial fail-open host regression FAILED: %s\n", message);
    exit(1);
}

static void fake_reset(enum fake_uart_mode mode) {
    fake_mode = mode;
    fake_loopback = false;
    fake_dlab = false;
    fake_loopback_byte = 0U;
    fake_line_status_reads = 0U;
    fake_data_writes = 0U;
}

uint8_t boring_serial_test_in8(uint16_t port) {
    if (port == (uint16_t)COM1_DATA) {
        if (fake_mode == FAKE_UART_ABSENT) {
            return 0xffU;
        }
        if (fake_loopback) {
            return fake_loopback_byte;
        }
        return 0U;
    }

    if (port == (uint16_t)COM1_LINE_STATUS) {
        ++fake_line_status_reads;
        if (fake_mode == FAKE_UART_READY) {
            return (uint8_t)COM1_LINE_STATUS_TX_READY;
        }
        return 0U;
    }

    return 0U;
}

void boring_serial_test_out8(uint16_t port, uint8_t value) {
    if (port == (uint16_t)COM1_LINE_CONTROL) {
        fake_dlab = (value & 0x80U) != 0U;
        return;
    }

    if (port == (uint16_t)COM1_MODEM_CONTROL) {
        fake_loopback = value == (uint8_t)SERIAL_MODEM_LOOPBACK;
        return;
    }

    if (port == (uint16_t)COM1_DATA) {
        if (fake_loopback) {
            fake_loopback_byte = value;
        } else if (!fake_dlab) {
            ++fake_data_writes;
        }
    }
}

static void test_ready_uart(void) {
    fake_reset(FAKE_UART_READY);
    serial_init();
    serial_write_string("OK");

    if (fake_data_writes != 2U) {
        fail("available UART did not preserve TX output");
    }
    if (fake_line_status_reads != 2U) {
        fail("available UART did not take the normal ready path");
    }
}

static void test_absent_uart(void) {
    size_t data_writes_before;
    size_t line_status_before;

    fake_reset(FAKE_UART_ABSENT);
    serial_init();
    data_writes_before = fake_data_writes;
    line_status_before = fake_line_status_reads;
    serial_write_string("ignored");

    if (fake_line_status_reads != line_status_before) {
        fail("absent UART still polled TX readiness");
    }
    if (fake_data_writes != data_writes_before) {
        fail("absent UART received diagnostic TX bytes");
    }
    if (serial_read_char_blocking() != '\0') {
        fail("absent UART blocking read did not fail open");
    }
}

static void test_non_ready_uart(void) {
    size_t first_poll_count;

    fake_reset(FAKE_UART_TX_STUCK);
    serial_init();
    serial_write_string("AB");
    first_poll_count = fake_line_status_reads;

    if ((first_poll_count == 0U) || (first_poll_count > 8192U)) {
        fail("non-ready UART TX polling was not bounded");
    }

    serial_write_string("C");
    if (fake_line_status_reads != first_poll_count) {
        fail("disabled UART repeated the TX timeout loop");
    }
    if (fake_data_writes != 0U) {
        fail("non-ready UART received a TX byte after timeout");
    }
}

int main(void) {
    test_ready_uart();
    test_absent_uart();
    test_non_ready_uart();
    puts("M61 serial fail-open host regression passed.");
    return 0;
}
