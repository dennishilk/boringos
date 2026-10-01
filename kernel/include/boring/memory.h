#ifndef BORING_KERNEL_MEMORY_H
#define BORING_KERNEL_MEMORY_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
void *memmove(void *destination, const void *source, size_t length);

#endif
