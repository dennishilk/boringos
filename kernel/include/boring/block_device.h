#ifndef BORING_BLOCK_DEVICE_H
#define BORING_BLOCK_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLOCK_DEVICE_MAX_DEVICES 8U

enum block_device_result {
    BLOCK_DEVICE_RESULT_OK = 0,
    BLOCK_DEVICE_RESULT_INVALID_ARGUMENT,
    BLOCK_DEVICE_RESULT_NOT_FOUND,
    BLOCK_DEVICE_RESULT_OUT_OF_RANGE,
    BLOCK_DEVICE_RESULT_READ_ONLY,
    BLOCK_DEVICE_RESULT_IO_ERROR,
    BLOCK_DEVICE_RESULT_REGISTRY_FULL,
    BLOCK_DEVICE_RESULT_ALREADY_REGISTERED
};

struct block_device_ops {
    enum block_device_result (*read)(void *context,
                                     uint64_t first_block,
                                     uint32_t block_count,
                                     void *buffer);
    enum block_device_result (*write)(void *context,
                                      uint64_t first_block,
                                      uint32_t block_count,
                                      const void *buffer);
};

struct block_device {
    const char *name;
    uint32_t logical_block_size;
    uint64_t block_count;
    bool read_only;
    void *context;
    const struct block_device_ops *ops;
};

void block_device_init(void);
enum block_device_result block_device_register(
    const struct block_device *device);
size_t block_device_count(void);
const struct block_device *block_device_get(size_t index);
const struct block_device *block_device_find(const char *name);
enum block_device_result block_device_read(const struct block_device *device,
                                           uint64_t first_block,
                                           uint32_t block_count,
                                           void *buffer);
enum block_device_result block_device_write(const struct block_device *device,
                                            uint64_t first_block,
                                            uint32_t block_count,
                                            const void *buffer);

#endif
