#ifndef BORING_SERIAL_H
#define BORING_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write_string(const char *text);
void serial_write_u64(uint64_t value);

#endif
