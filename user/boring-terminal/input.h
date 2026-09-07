#ifndef BORING_TERMINAL_INPUT_H
#define BORING_TERMINAL_INPUT_H

#include <stddef.h>
#include <stdint.h>

#define BORING_TERMINAL_KEY_BYTES_MAX 4U

size_t boring_terminal_key_bytes(uint32_t code, uint32_t modifiers,
                                 char output[BORING_TERMINAL_KEY_BYTES_MAX]);

#endif
