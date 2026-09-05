#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>

static const struct block_device *block_devices[BLOCK_DEVICE_MAX_DEVICES];
static size_t block_devices_count;

static bool block_device_name_equal(const char *left, const char *right) {
    size_t index = 0U;

    if ((left == NULL) || (right == NULL)) {
        return false;
    }

    while ((left[index] != '\0') && (right[index] != '\0')) {
        if (left[index] != right[index]) {
            return false;
        }
        ++index;
    }

    return left[index] == right[index];
}

static bool block_device_is_registered(const struct block_device *device) {
    size_t index;

    if (device == NULL) {
        return false;
    }

    for (index = 0U; index < block_devices_count; ++index) {
        if (block_devices[index] == device) {
            return true;
        }
    }

    return false;
}

static enum block_device_result block_device_validate_descriptor(
    const struct block_device *device) {
    if ((device == NULL) || (device->name == NULL) ||
        (device->name[0] == '\0') || (device->ops == NULL) ||
        (device->ops->read == NULL) || (device->logical_block_size == 0U) ||
        (device->block_count == 0ULL)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }

    if (device->block_count >
        (UINT64_MAX / (uint64_t)device->logical_block_size)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }

    if ((!device->read_only) && (device->ops->write == NULL)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }

    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result block_device_validate_request(
    const struct block_device *device,
    uint64_t first_block,
    uint32_t block_count,
    const void *buffer) {
    if ((device == NULL) || (buffer == NULL) || (block_count == 0U)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }

    if (block_device_validate_descriptor(device) != BLOCK_DEVICE_RESULT_OK) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }

    if (!block_device_is_registered(device)) {
        return BLOCK_DEVICE_RESULT_NOT_FOUND;
    }

    if ((device->block_count == 0ULL) ||
        (first_block >= device->block_count) ||
        ((uint64_t)block_count > (device->block_count - first_block))) {
        return BLOCK_DEVICE_RESULT_OUT_OF_RANGE;
    }

    return BLOCK_DEVICE_RESULT_OK;
}

void block_device_init(void) {
    size_t index;

    for (index = 0U; index < (size_t)BLOCK_DEVICE_MAX_DEVICES; ++index) {
        block_devices[index] = NULL;
    }
    block_devices_count = 0U;
}

enum block_device_result block_device_register(
    const struct block_device *device) {
    enum block_device_result validation;
    size_t index;

    validation = block_device_validate_descriptor(device);
    if (validation != BLOCK_DEVICE_RESULT_OK) {
        return validation;
    }

    for (index = 0U; index < block_devices_count; ++index) {
        if ((block_devices[index] == device) ||
            block_device_name_equal(block_devices[index]->name, device->name)) {
            return BLOCK_DEVICE_RESULT_ALREADY_REGISTERED;
        }
    }

    if (block_devices_count >= (size_t)BLOCK_DEVICE_MAX_DEVICES) {
        return BLOCK_DEVICE_RESULT_REGISTRY_FULL;
    }

    block_devices[block_devices_count] = device;
    ++block_devices_count;
    return BLOCK_DEVICE_RESULT_OK;
}

size_t block_device_count(void) {
    return block_devices_count;
}

const struct block_device *block_device_get(size_t index) {
    if (index >= block_devices_count) {
        return NULL;
    }
    return block_devices[index];
}

const struct block_device *block_device_find(const char *name) {
    size_t index;

    if ((name == NULL) || (name[0] == '\0')) {
        return NULL;
    }

    for (index = 0U; index < block_devices_count; ++index) {
        if (block_device_name_equal(block_devices[index]->name, name)) {
            return block_devices[index];
        }
    }

    return NULL;
}

enum block_device_result block_device_read(const struct block_device *device,
                                           uint64_t first_block,
                                           uint32_t block_count,
                                           void *buffer) {
    enum block_device_result validation = block_device_validate_request(
        device, first_block, block_count, buffer);

    if (validation != BLOCK_DEVICE_RESULT_OK) {
        return validation;
    }

    if ((device->ops == NULL) || (device->ops->read == NULL)) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    return device->ops->read(device->context, first_block, block_count, buffer);
}

enum block_device_result block_device_write(const struct block_device *device,
                                            uint64_t first_block,
                                            uint32_t block_count,
                                            const void *buffer) {
    enum block_device_result validation = block_device_validate_request(
        device, first_block, block_count, buffer);

    if (validation != BLOCK_DEVICE_RESULT_OK) {
        return validation;
    }

    if (device->read_only) {
        return BLOCK_DEVICE_RESULT_READ_ONLY;
    }

    if ((device->ops == NULL) || (device->ops->write == NULL)) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    return device->ops->write(device->context, first_block, block_count, buffer);
}


enum block_device_result block_device_flush(const struct block_device *device) {
    enum block_device_result validation;

    if (device == NULL) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }
    validation = block_device_validate_descriptor(device);
    if (validation != BLOCK_DEVICE_RESULT_OK) {
        return validation;
    }
    if (!block_device_is_registered(device)) {
        return BLOCK_DEVICE_RESULT_NOT_FOUND;
    }
    if (device->read_only || (device->ops->flush == NULL)) {
        return BLOCK_DEVICE_RESULT_OK;
    }
    return device->ops->flush(device->context);
}

enum block_device_result block_device_flush_all(void) {
    size_t index;

    for (index = 0U; index < block_devices_count; ++index) {
        const enum block_device_result result =
            block_device_flush(block_devices[index]);
        if (result != BLOCK_DEVICE_RESULT_OK) {
            return result;
        }
    }
    return BLOCK_DEVICE_RESULT_OK;
}
