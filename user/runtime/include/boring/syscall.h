#ifndef BORING_USER_SYSCALL_H
#define BORING_USER_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

uint64_t boring_getpid(void);
long boring_debug_write(const void *buffer, size_t length);

#endif
