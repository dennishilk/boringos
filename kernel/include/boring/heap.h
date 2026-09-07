#ifndef BORING_HEAP_H
#define BORING_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_HEAP_ALIGNMENT 16ULL

struct heap_stats {
    uintptr_t virtual_base;
    uintptr_t virtual_limit;
    uint64_t mapped_pages;
    uint64_t mapped_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t allocation_count;
};

bool heap_init(void);
void *kmalloc(size_t size);
bool kfree(void *ptr);
bool heap_get_stats(struct heap_stats *stats);

#endif
