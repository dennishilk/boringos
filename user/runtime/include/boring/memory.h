#ifndef BORING_USER_MEMORY_H
#define BORING_USER_MEMORY_H

#include <stddef.h>

void *boring_memcpy(void *destination, const void *source, size_t length);
void *boring_memset(void *destination, int value, size_t length);

/* Minimal BoringOS-native heap. This is not a claim of full libc malloc ABI. */
void *boring_malloc(size_t size);
void *boring_calloc(size_t count, size_t size);
void boring_free(void *pointer);

#endif
