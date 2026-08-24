#ifndef BORING_USER_MEMORY_H
#define BORING_USER_MEMORY_H

#include <stddef.h>

void *boring_memcpy(void *destination, const void *source, size_t length);
void *boring_memset(void *destination, int value, size_t length);

#endif
