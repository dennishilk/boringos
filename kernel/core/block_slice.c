#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/block_slice.h>

static enum block_device_result slice_read(void *context,
                                           uint64_t first_block,
                                           uint32_t block_count,
                                           void *buffer);
static enum block_device_result slice_write(void *context,
                                            uint64_t first_block,
                                            uint32_t block_count,
                                            const void *buffer);

static const struct block_device_ops slice_ops = {
    .read = slice_read,
    .write = slice_write
};

bool block_device_slice_translate(uint64_t parent_first_block,
                                  uint64_t slice_block_count,
                                  uint64_t first_block,
                                  uint32_t block_count,
                                  uint64_t *parent_first_out) {
    if ((parent_first_out == NULL) || (slice_block_count == 0ULL) ||
        (block_count == 0U) || (first_block >= slice_block_count) ||
        ((uint64_t)block_count > slice_block_count - first_block) ||
        (parent_first_block > UINT64_MAX - first_block)) {
        return false;
    }
    *parent_first_out = parent_first_block + first_block;
    return true;
}

static enum block_device_result slice_read(void *context,
                                           uint64_t first_block,
                                           uint32_t block_count,
                                           void *buffer) {
    struct block_device_slice *slice = (struct block_device_slice *)context;
    uint64_t parent_first;

    if ((slice == NULL) || (slice->parent == NULL) ||
        !block_device_slice_translate(slice->parent_first_block,
                                      slice->device.block_count,
                                      first_block, block_count,
                                      &parent_first)) {
        return BLOCK_DEVICE_RESULT_OUT_OF_RANGE;
    }
    return block_device_read(slice->parent, parent_first, block_count, buffer);
}

static enum block_device_result slice_write(void *context,
                                            uint64_t first_block,
                                            uint32_t block_count,
                                            const void *buffer) {
    struct block_device_slice *slice = (struct block_device_slice *)context;
    uint64_t parent_first;

    if ((slice == NULL) || (slice->parent == NULL) ||
        !block_device_slice_translate(slice->parent_first_block,
                                      slice->device.block_count,
                                      first_block, block_count,
                                      &parent_first)) {
        return BLOCK_DEVICE_RESULT_OUT_OF_RANGE;
    }
    return block_device_write(slice->parent, parent_first, block_count, buffer);
}

enum block_device_result block_device_slice_register(
    struct block_device_slice *slice,
    const char *name,
    const struct block_device *parent,
    uint64_t parent_first_block,
    uint64_t block_count,
    bool read_only) {
    if ((slice == NULL) || (name == NULL) || (name[0] == '\0') ||
        (parent == NULL) || (parent->name == NULL) ||
        (parent->logical_block_size == 0U) || (parent->block_count == 0ULL)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }
    if ((block_device_find(parent->name) != parent) ||
        (parent_first_block >= parent->block_count) ||
        (block_count == 0ULL) ||
        (block_count > parent->block_count - parent_first_block)) {
        return BLOCK_DEVICE_RESULT_OUT_OF_RANGE;
    }
    if (parent->read_only && !read_only) {
        return BLOCK_DEVICE_RESULT_READ_ONLY;
    }

    slice->parent = parent;
    slice->parent_first_block = parent_first_block;
    slice->device.name = name;
    slice->device.logical_block_size = parent->logical_block_size;
    slice->device.block_count = block_count;
    slice->device.read_only = read_only || parent->read_only;
    slice->device.context = slice;
    slice->device.ops = &slice_ops;
    return block_device_register(&slice->device);
}
