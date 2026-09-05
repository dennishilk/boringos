#ifndef BORING_SYSTEM_CONTROL_H
#define BORING_SYSTEM_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

bool system_control_spawn_allowed(void);
bool system_control_begin(uint32_t action);
void system_control_execute(uint32_t action) __attribute__((noreturn));

#endif
