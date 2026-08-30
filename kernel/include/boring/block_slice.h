#ifndef BORING_BLOCK_SLICE_H
#define BORING_BLOCK_SLICE_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/block_device.h>

struct block_device_slice {
    struct block_device device;
    const struct block_device *parent;
    uint64_t parent_first_block;
};

bool block_device_slice_translate(uint64_t parent_first_block,
                                  uint64_t slice_block_count,
                                  uint64_t first_block,
                                  uint32_t block_count,
                                  uint64_t *parent_first_out);

enum block_device_result block_device_slice_register(
    struct block_device_slice *slice,
    const char *name,
    const struct block_device *parent,
    uint64_t parent_first_block,
    uint64_t block_count,
    bool read_only);

#endif
