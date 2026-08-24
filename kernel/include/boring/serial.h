#ifndef BORING_SERIAL_H
#define BORING_SERIAL_H

#include <stddef.h>
#include <stdint.h>

void serial_init(void);
void serial_write_bytes(const char *data, size_t length);
void serial_write_string(const char *text);
char serial_read_char_blocking(void);
void serial_write_u64(uint64_t value);
void serial_write_hex_u64(uint64_t value);

#endif
