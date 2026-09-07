#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/block_device.h>
#include <boring/block_slice.h>

struct fake_backend {
    uint64_t last_first;
    uint32_t last_count;
    uint32_t reads;
    uint32_t writes;
};

static enum block_device_result fake_read(void *context,
                                          uint64_t first_block,
                                          uint32_t block_count,
                                          void *buffer) {
    struct fake_backend *backend = (struct fake_backend *)context;
    if ((backend == NULL) || (buffer == NULL)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }
    backend->last_first = first_block;
    backend->last_count = block_count;
    ++backend->reads;
    ((uint8_t *)buffer)[0] = 0x61U;
    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result fake_write(void *context,
                                           uint64_t first_block,
                                           uint32_t block_count,
                                           const void *buffer) {
    struct fake_backend *backend = (struct fake_backend *)context;
    if ((backend == NULL) || (buffer == NULL)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }
    backend->last_first = first_block;
    backend->last_count = block_count;
    ++backend->writes;
    return BLOCK_DEVICE_RESULT_OK;
}

static const struct block_device_ops fake_ops = {
    .read = fake_read,
    .write = fake_write
};

static bool expect(bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "M61 slice host test FAILED: %s\n", message);
        return false;
    }
    return true;
}

int main(void) {
    struct fake_backend backend = {0};
    struct block_device parent = {
        .name = "usb0",
        .logical_block_size = 512U,
        .block_count = 64ULL,
        .read_only = false,
        .context = &backend,
        .ops = &fake_ops
    };
    struct block_device_slice slice = {0};
    struct block_device_slice rejected = {0};
    uint64_t translated = 0ULL;
    uint8_t byte = 0U;

    block_device_init();
    if (!expect(block_device_register(&parent) == BLOCK_DEVICE_RESULT_OK,
                "parent registration") ||
        !expect(block_device_slice_register(&slice, "root", &parent,
                                            16ULL, 32ULL, false) ==
                    BLOCK_DEVICE_RESULT_OK,
                "valid root slice") ||
        !expect(block_device_read(&slice.device, 0ULL, 1U, &byte) ==
                    BLOCK_DEVICE_RESULT_OK && backend.last_first == 16ULL &&
                    backend.last_count == 1U && byte == 0x61U,
                "logical LBA zero translation") ||
        !expect(block_device_write(&slice.device, 31ULL, 1U, &byte) ==
                    BLOCK_DEVICE_RESULT_OK && backend.last_first == 47ULL,
                "last logical LBA translation") ||
        !expect(block_device_read(&slice.device, 32ULL, 1U, &byte) ==
                    BLOCK_DEVICE_RESULT_OUT_OF_RANGE,
                "logical LBA outside bounded root") ||
        !expect(block_device_slice_register(&rejected, "bad-start", &parent,
                                            64ULL, 1ULL, false) ==
                    BLOCK_DEVICE_RESULT_OUT_OF_RANGE,
                "root start beyond parent capacity") ||
        !expect(block_device_slice_register(&rejected, "bad-length", &parent,
                                            63ULL, 2ULL, false) ==
                    BLOCK_DEVICE_RESULT_OUT_OF_RANGE,
                "root length beyond parent capacity") ||
        !expect(block_device_slice_register(&rejected, "no-usb", NULL,
                                            0ULL, 1ULL, false) ==
                    BLOCK_DEVICE_RESULT_INVALID_ARGUMENT,
                "unavailable usb0 fails closed") ||
        !expect(!block_device_slice_translate(UINT64_MAX - 1ULL, 4ULL,
                                              3ULL, 1U, &translated),
                "parent LBA integer overflow rejected")) {
        return 1;
    }

    (void)printf("M61 root start beyond usb0 capacity: REJECTED\n");
    (void)printf("M61 root length exceeding usb0 capacity: REJECTED\n");
    (void)printf("M61 logical LBA outside bounded root: REJECTED\n");
    (void)printf("M61 root LBA translation overflow: REJECTED\n");
    (void)printf("M61 unavailable usb0 root mode: REJECTED\n");
    (void)printf("M61 bounded block-device slice host test passed.\n");
    return 0;
}
