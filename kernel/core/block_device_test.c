#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/block_device_test.h>
#include <boring/cpu.h>
#include <boring/serial.h>

#define TESTBLK0_BLOCK_SIZE 512U
#define TESTBLK0_BLOCKS 64ULL
#define TESTBLK0_BYTES ((size_t)TESTBLK0_BLOCK_SIZE * (size_t)TESTBLK0_BLOCKS)
#define TESTBLK_RO_BLOCK_SIZE 4096U
#define TESTBLK_RO_BLOCKS 8ULL
#define TESTBLK_RO_BYTES ((size_t)TESTBLK_RO_BLOCK_SIZE * (size_t)TESTBLK_RO_BLOCKS)
#define TEST_MULTI_BLOCKS 3U
#define TEST_SINGLE_BLOCK 10ULL
#define TEST_MULTI_FIRST_BLOCK 20ULL

struct block_test_backend {
    uint8_t *storage;
    size_t storage_size;
    uint32_t logical_block_size;
    uint64_t block_count;
    uint64_t read_calls;
    uint64_t write_calls;
    bool fail_read;
    bool fail_write;
};

static uint8_t testblk0_storage[TESTBLK0_BYTES];
static uint8_t testblk_ro_storage[TESTBLK_RO_BYTES];

static struct block_test_backend testblk0_backend = {
    .storage = testblk0_storage,
    .storage_size = TESTBLK0_BYTES,
    .logical_block_size = TESTBLK0_BLOCK_SIZE,
    .block_count = TESTBLK0_BLOCKS,
    .read_calls = 0ULL,
    .write_calls = 0ULL,
    .fail_read = false,
    .fail_write = false
};

static struct block_test_backend testblk_ro_backend = {
    .storage = testblk_ro_storage,
    .storage_size = TESTBLK_RO_BYTES,
    .logical_block_size = TESTBLK_RO_BLOCK_SIZE,
    .block_count = TESTBLK_RO_BLOCKS,
    .read_calls = 0ULL,
    .write_calls = 0ULL,
    .fail_read = false,
    .fail_write = false
};

static bool block_test_range_to_bytes(const struct block_test_backend *backend,
                                      uint64_t first_block,
                                      uint32_t block_count,
                                      size_t *offset_out,
                                      size_t *length_out) {
    uint64_t offset;
    uint64_t length;

    if ((backend == NULL) || (offset_out == NULL) || (length_out == NULL) ||
        (block_count == 0U) || (first_block >= backend->block_count) ||
        ((uint64_t)block_count > (backend->block_count - first_block))) {
        return false;
    }

    offset = first_block * (uint64_t)backend->logical_block_size;
    length = (uint64_t)block_count * (uint64_t)backend->logical_block_size;
    if ((offset > (uint64_t)SIZE_MAX) || (length > (uint64_t)SIZE_MAX) ||
        ((size_t)offset > backend->storage_size) ||
        ((size_t)length > (backend->storage_size - (size_t)offset))) {
        return false;
    }

    *offset_out = (size_t)offset;
    *length_out = (size_t)length;
    return true;
}

static enum block_device_result block_test_read(void *context,
                                                uint64_t first_block,
                                                uint32_t block_count,
                                                void *buffer) {
    struct block_test_backend *backend = (struct block_test_backend *)context;
    uint8_t *destination = (uint8_t *)buffer;
    size_t offset;
    size_t length;
    size_t index;

    if ((backend == NULL) || (buffer == NULL)) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    ++backend->read_calls;
    if (backend->fail_read) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    if (!block_test_range_to_bytes(backend, first_block, block_count,
                                   &offset, &length)) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    for (index = 0U; index < length; ++index) {
        destination[index] = backend->storage[offset + index];
    }
    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result block_test_write(void *context,
                                                 uint64_t first_block,
                                                 uint32_t block_count,
                                                 const void *buffer) {
    struct block_test_backend *backend = (struct block_test_backend *)context;
    const uint8_t *source = (const uint8_t *)buffer;
    size_t offset;
    size_t length;
    size_t index;

    if ((backend == NULL) || (buffer == NULL)) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    ++backend->write_calls;
    if (backend->fail_write) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    if (!block_test_range_to_bytes(backend, first_block, block_count,
                                   &offset, &length)) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    for (index = 0U; index < length; ++index) {
        backend->storage[offset + index] = source[index];
    }
    return BLOCK_DEVICE_RESULT_OK;
}

static const struct block_device_ops test_writable_ops = {
    .read = block_test_read,
    .write = block_test_write
};

static const struct block_device_ops test_read_only_ops = {
    .read = block_test_read,
    .write = block_test_write
};

static const struct block_device_ops test_read_only_no_write_ops = {
    .read = block_test_read,
    .write = NULL
};

static const struct block_device_ops test_missing_read_ops = {
    .read = NULL,
    .write = block_test_write
};

static const struct block_device testblk0 = {
    .name = "testblk0",
    .logical_block_size = TESTBLK0_BLOCK_SIZE,
    .block_count = TESTBLK0_BLOCKS,
    .read_only = false,
    .context = &testblk0_backend,
    .ops = &test_writable_ops
};

static const struct block_device testblk_ro = {
    .name = "testblk-ro",
    .logical_block_size = TESTBLK_RO_BLOCK_SIZE,
    .block_count = TESTBLK_RO_BLOCKS,
    .read_only = true,
    .context = &testblk_ro_backend,
    .ops = &test_read_only_ops
};

static const struct block_device filler_devices[6] = {
    { "test-fill0", 512U, 1ULL, true, &testblk0_backend, &test_read_only_no_write_ops },
    { "test-fill1", 512U, 1ULL, true, &testblk0_backend, &test_read_only_no_write_ops },
    { "test-fill2", 512U, 1ULL, true, &testblk0_backend, &test_read_only_no_write_ops },
    { "test-fill3", 512U, 1ULL, true, &testblk0_backend, &test_read_only_no_write_ops },
    { "test-fill4", 512U, 1ULL, true, &testblk0_backend, &test_read_only_no_write_ops },
    { "test-fill5", 512U, 1ULL, true, &testblk0_backend, &test_read_only_no_write_ops }
};

static const struct block_device overflow_device = {
    .name = "test-overflow",
    .logical_block_size = 512U,
    .block_count = 1ULL,
    .read_only = true,
    .context = &testblk0_backend,
    .ops = &test_read_only_no_write_ops
};

static uint8_t block_test_expected_byte(uint8_t seed,
                                        uint64_t block,
                                        uint32_t byte_index) {
    const uint64_t value = (uint64_t)seed + (block * 17ULL) +
                           ((uint64_t)byte_index * 3ULL);
    return (uint8_t)(value & 0xffULL);
}

static void block_test_initialize_backend(struct block_test_backend *backend,
                                          uint8_t seed) {
    uint64_t block;

    backend->read_calls = 0ULL;
    backend->write_calls = 0ULL;
    backend->fail_read = false;
    backend->fail_write = false;

    for (block = 0ULL; block < backend->block_count; ++block) {
        uint32_t byte_index;
        const size_t offset = (size_t)(block *
            (uint64_t)backend->logical_block_size);

        for (byte_index = 0U; byte_index < backend->logical_block_size;
             ++byte_index) {
            backend->storage[offset + (size_t)byte_index] =
                block_test_expected_byte(seed, block, byte_index);
        }
    }
}

static bool block_test_buffer_equals(const uint8_t *left,
                                     const uint8_t *right,
                                     size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static bool block_test_matches_initial_pattern(const uint8_t *buffer,
                                               uint8_t seed,
                                               uint64_t first_block,
                                               uint32_t block_count,
                                               uint32_t block_size) {
    uint32_t relative_block;

    for (relative_block = 0U; relative_block < block_count;
         ++relative_block) {
        uint32_t byte_index;

        for (byte_index = 0U; byte_index < block_size; ++byte_index) {
            const size_t offset =
                ((size_t)relative_block * (size_t)block_size) +
                (size_t)byte_index;
            if (buffer[offset] != block_test_expected_byte(
                    seed, first_block + (uint64_t)relative_block,
                    byte_index)) {
                return false;
            }
        }
    }
    return true;
}

static void block_test_fill_write_pattern(uint8_t *buffer,
                                          size_t length,
                                          uint8_t seed) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        buffer[index] = (uint8_t)(((uint64_t)seed +
                                   ((uint64_t)index * 29ULL)) & 0xffULL);
    }
}

static void block_device_test_fail(const char *check) __attribute__((noreturn));
static void block_device_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("BoringKernel block-device test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void block_test_registry_and_registration(void) {
    static const struct block_device duplicate_name = {
        .name = "testblk0",
        .logical_block_size = 512U,
        .block_count = 1ULL,
        .read_only = true,
        .context = &testblk0_backend,
        .ops = &test_read_only_no_write_ops
    };
    static const struct block_device empty_name = {
        .name = "",
        .logical_block_size = 512U,
        .block_count = 1ULL,
        .read_only = true,
        .context = &testblk0_backend,
        .ops = &test_read_only_no_write_ops
    };
    static const struct block_device missing_ops = {
        .name = "invalid-ops",
        .logical_block_size = 512U,
        .block_count = 1ULL,
        .read_only = true,
        .context = &testblk0_backend,
        .ops = NULL
    };
    static const struct block_device missing_read = {
        .name = "invalid-read",
        .logical_block_size = 512U,
        .block_count = 1ULL,
        .read_only = true,
        .context = &testblk0_backend,
        .ops = &test_missing_read_ops
    };
    static const struct block_device missing_write = {
        .name = "invalid-write",
        .logical_block_size = 512U,
        .block_count = 1ULL,
        .read_only = false,
        .context = &testblk0_backend,
        .ops = &test_read_only_no_write_ops
    };
    static const struct block_device zero_block_size = {
        .name = "invalid-size",
        .logical_block_size = 0U,
        .block_count = 1ULL,
        .read_only = true,
        .context = &testblk0_backend,
        .ops = &test_read_only_no_write_ops
    };
    static const struct block_device zero_blocks = {
        .name = "invalid-count",
        .logical_block_size = 512U,
        .block_count = 0ULL,
        .read_only = true,
        .context = &testblk0_backend,
        .ops = &test_read_only_no_write_ops
    };
    static const struct block_device capacity_overflow = {
        .name = "invalid-capacity",
        .logical_block_size = UINT32_MAX,
        .block_count = UINT64_MAX,
        .read_only = true,
        .context = &testblk0_backend,
        .ops = &test_read_only_no_write_ops
    };

    serial_write_string("Registry:\n");
    block_device_init();
    if (block_device_count() != 0U) {
        block_device_test_fail("initial-state");
    }
    serial_write_string("  initial-state: PASS\n");

    if ((block_device_register(&testblk0) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_register(&testblk_ro) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_count() != 2U)) {
        block_device_test_fail("register");
    }
    serial_write_string("  register: PASS\n");

    if ((block_device_get(0U) != &testblk0) ||
        (block_device_get(1U) != &testblk_ro) ||
        (block_device_get(2U) != NULL)) {
        block_device_test_fail("lookup-index");
    }
    serial_write_string("  lookup-index: PASS\n");

    if ((block_device_find("testblk0") != &testblk0) ||
        (block_device_find("testblk-ro") != &testblk_ro) ||
        (block_device_find("unknown") != NULL)) {
        block_device_test_fail("lookup-name");
    }
    serial_write_string("  lookup-name: PASS\n");

    if ((block_device_register(&testblk0) !=
         BLOCK_DEVICE_RESULT_ALREADY_REGISTERED) ||
        (block_device_register(&duplicate_name) !=
         BLOCK_DEVICE_RESULT_ALREADY_REGISTERED) ||
        (block_device_count() != 2U)) {
        block_device_test_fail("duplicate-reject");
    }
    serial_write_string("  duplicate-reject: PASS\n");

    if ((block_device_register(NULL) != BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_register(&empty_name) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_register(&missing_ops) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_register(&missing_read) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_register(&missing_write) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_register(&zero_block_size) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_register(&zero_blocks) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_register(&capacity_overflow) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_count() != 2U)) {
        block_device_test_fail("invalid-geometry");
    }
    serial_write_string("  invalid-geometry: PASS\n");
}

static void block_test_device_io(void) {
    uint8_t initial[TESTBLK0_BLOCK_SIZE];
    uint8_t single_write[TESTBLK0_BLOCK_SIZE];
    uint8_t single_read[TESTBLK0_BLOCK_SIZE];
    uint8_t single_left_before[TESTBLK0_BLOCK_SIZE];
    uint8_t single_left_after[TESTBLK0_BLOCK_SIZE];
    uint8_t single_right_before[TESTBLK0_BLOCK_SIZE];
    uint8_t single_right_after[TESTBLK0_BLOCK_SIZE];
    uint8_t multi_write[TESTBLK0_BLOCK_SIZE * TEST_MULTI_BLOCKS];
    uint8_t multi_read[TESTBLK0_BLOCK_SIZE * TEST_MULTI_BLOCKS];
    uint8_t multi_left_before[TESTBLK0_BLOCK_SIZE];
    uint8_t multi_left_after[TESTBLK0_BLOCK_SIZE];
    uint8_t multi_right_before[TESTBLK0_BLOCK_SIZE];
    uint8_t multi_right_after[TESTBLK0_BLOCK_SIZE];

    serial_write_string("\nDevice testblk0:\n");
    serial_write_string("  logical block size: ");
    serial_write_u64((uint64_t)testblk0.logical_block_size);
    serial_write_string("\n  blocks: ");
    serial_write_u64(testblk0.block_count);
    serial_write_string("\n  capacity: ");
    serial_write_u64(testblk0.block_count *
                     (uint64_t)testblk0.logical_block_size);
    serial_write_string(" bytes\n");

    if ((block_device_read(&testblk0, 3ULL, 1U, initial) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !block_test_matches_initial_pattern(initial, 0x21U, 3ULL, 1U,
                                            TESTBLK0_BLOCK_SIZE)) {
        block_device_test_fail("initial-read");
    }
    serial_write_string("  initial-read: PASS\n");

    if ((block_device_read(&testblk0, TEST_SINGLE_BLOCK - 1ULL, 1U,
                           single_left_before) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(&testblk0, TEST_SINGLE_BLOCK + 1ULL, 1U,
                           single_right_before) != BLOCK_DEVICE_RESULT_OK)) {
        block_device_test_fail("neighbor-preservation");
    }

    block_test_fill_write_pattern(single_write, sizeof(single_write), 0x51U);
    if (block_device_write(&testblk0, TEST_SINGLE_BLOCK, 1U, single_write) !=
        BLOCK_DEVICE_RESULT_OK) {
        block_device_test_fail("single-block-write");
    }
    serial_write_string("  single-block-write: PASS\n");

    if ((block_device_read(&testblk0, TEST_SINGLE_BLOCK, 1U, single_read) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !block_test_buffer_equals(single_write, single_read,
                                  sizeof(single_write))) {
        block_device_test_fail("single-block-read-back");
    }
    serial_write_string("  single-block-read-back: PASS\n");

    if ((block_device_read(&testblk0, TEST_SINGLE_BLOCK - 1ULL, 1U,
                           single_left_after) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(&testblk0, TEST_SINGLE_BLOCK + 1ULL, 1U,
                           single_right_after) != BLOCK_DEVICE_RESULT_OK) ||
        !block_test_buffer_equals(single_left_before, single_left_after,
                                  sizeof(single_left_before)) ||
        !block_test_buffer_equals(single_right_before, single_right_after,
                                  sizeof(single_right_before))) {
        block_device_test_fail("neighbor-preservation");
    }

    if ((block_device_read(&testblk0, TEST_MULTI_FIRST_BLOCK - 1ULL, 1U,
                           multi_left_before) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(&testblk0,
                           TEST_MULTI_FIRST_BLOCK + (uint64_t)TEST_MULTI_BLOCKS,
                           1U, multi_right_before) != BLOCK_DEVICE_RESULT_OK)) {
        block_device_test_fail("neighbor-preservation");
    }

    block_test_fill_write_pattern(multi_write, sizeof(multi_write), 0xa3U);
    if ((block_device_write(&testblk0, TEST_MULTI_FIRST_BLOCK,
                            TEST_MULTI_BLOCKS, multi_write) !=
         BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(&testblk0, TEST_MULTI_FIRST_BLOCK,
                           TEST_MULTI_BLOCKS, multi_read) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !block_test_buffer_equals(multi_write, multi_read,
                                  sizeof(multi_write))) {
        block_device_test_fail("multi-block-write-read-back");
    }
    serial_write_string("  multi-block-write-read-back: PASS\n");

    if ((block_device_read(&testblk0, TEST_MULTI_FIRST_BLOCK - 1ULL, 1U,
                           multi_left_after) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(&testblk0,
                           TEST_MULTI_FIRST_BLOCK + (uint64_t)TEST_MULTI_BLOCKS,
                           1U, multi_right_after) != BLOCK_DEVICE_RESULT_OK) ||
        !block_test_buffer_equals(multi_left_before, multi_left_after,
                                  sizeof(multi_left_before)) ||
        !block_test_buffer_equals(multi_right_before, multi_right_after,
                                  sizeof(multi_right_before))) {
        block_device_test_fail("neighbor-preservation");
    }
    serial_write_string("  neighbor-preservation: PASS\n");
}

static void block_test_bounds(void) {
    static const struct block_device unregistered = {
        .name = "unregistered",
        .logical_block_size = 512U,
        .block_count = TESTBLK0_BLOCKS,
        .read_only = false,
        .context = &testblk0_backend,
        .ops = &test_writable_ops
    };
    uint8_t one_block[TESTBLK0_BLOCK_SIZE];
    uint8_t two_blocks[TESTBLK0_BLOCK_SIZE * 2U];
    const uint64_t reads_before_rejects = testblk0_backend.read_calls;
    const uint64_t writes_before_rejects = testblk0_backend.write_calls;

    serial_write_string("\nBounds:\n");

    if ((block_device_read(&testblk0, 0ULL, 1U, one_block) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !block_test_matches_initial_pattern(one_block, 0x21U, 0ULL, 1U,
                                            TESTBLK0_BLOCK_SIZE)) {
        block_device_test_fail("first-valid-block");
    }
    serial_write_string("  first-valid-block: PASS\n");

    if ((block_device_read(&testblk0, TESTBLK0_BLOCKS - 1ULL, 1U, one_block) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !block_test_matches_initial_pattern(one_block, 0x21U,
                                            TESTBLK0_BLOCKS - 1ULL, 1U,
                                            TESTBLK0_BLOCK_SIZE)) {
        block_device_test_fail("last-valid-block");
    }
    serial_write_string("  last-valid-block: PASS\n");

    if ((block_device_read(&testblk0, TESTBLK0_BLOCKS - 2ULL, 2U, two_blocks) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !block_test_matches_initial_pattern(two_blocks, 0x21U,
                                            TESTBLK0_BLOCKS - 2ULL, 2U,
                                            TESTBLK0_BLOCK_SIZE)) {
        block_device_test_fail("exact-end-range");
    }
    serial_write_string("  exact-end-range: PASS\n");

    {
        const uint64_t reads_after_valid = testblk0_backend.read_calls;
        const uint64_t writes_after_valid = testblk0_backend.write_calls;

        if ((block_device_read(&testblk0, 0ULL, 0U, one_block) !=
             BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
            (block_device_write(&testblk0, 0ULL, 0U, one_block) !=
             BLOCK_DEVICE_RESULT_INVALID_ARGUMENT)) {
            block_device_test_fail("zero-count");
        }
        serial_write_string("  zero-count: PASS\n");

        if ((block_device_read(&testblk0, 0ULL, 1U, NULL) !=
             BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
            (block_device_write(&testblk0, 0ULL, 1U, NULL) !=
             BLOCK_DEVICE_RESULT_INVALID_ARGUMENT)) {
            block_device_test_fail("null-buffer");
        }
        serial_write_string("  null-buffer: PASS\n");

        if ((block_device_read(&testblk0, TESTBLK0_BLOCKS, 1U, one_block) !=
             BLOCK_DEVICE_RESULT_OUT_OF_RANGE) ||
            (block_device_read(&testblk0, TESTBLK0_BLOCKS + 1ULL, 1U,
                               one_block) != BLOCK_DEVICE_RESULT_OUT_OF_RANGE) ||
            (block_device_read(&testblk0, TESTBLK0_BLOCKS - 1ULL, 2U,
                               two_blocks) != BLOCK_DEVICE_RESULT_OUT_OF_RANGE) ||
            (block_device_read(&testblk0, UINT64_MAX, 1U, one_block) !=
             BLOCK_DEVICE_RESULT_OUT_OF_RANGE) ||
            (block_device_read(&testblk0, 0ULL, UINT32_MAX, one_block) !=
             BLOCK_DEVICE_RESULT_OUT_OF_RANGE)) {
            block_device_test_fail("range-past-end");
        }
        serial_write_string("  first-past-end: PASS\n");
        serial_write_string("  range-past-end: PASS\n");
        serial_write_string("  overflow-safe-extremes: PASS\n");

        if ((block_device_read(NULL, 0ULL, 1U, one_block) !=
             BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
            (block_device_read(&unregistered, 0ULL, 1U, one_block) !=
             BLOCK_DEVICE_RESULT_NOT_FOUND)) {
            block_device_test_fail("invalid-device");
        }
        serial_write_string("  invalid-device: PASS\n");

        if ((testblk0_backend.read_calls != reads_after_valid) ||
            (testblk0_backend.write_calls != writes_after_valid)) {
            block_device_test_fail("rejected-request-no-backend-call");
        }
    }

    if ((testblk0_backend.read_calls < reads_before_rejects) ||
        (testblk0_backend.write_calls < writes_before_rejects)) {
        block_device_test_fail("rejected-request-no-backend-call");
    }
    serial_write_string("  rejected-request-no-backend-call: PASS\n");
}

static void block_test_read_only(void) {
    uint8_t before[TESTBLK_RO_BLOCK_SIZE];
    uint8_t after[TESTBLK_RO_BLOCK_SIZE];
    uint8_t replacement[TESTBLK_RO_BLOCK_SIZE];
    const uint64_t writes_before = testblk_ro_backend.write_calls;

    serial_write_string("\nRead-only testblk-ro:\n");
    serial_write_string("  logical block size: ");
    serial_write_u64((uint64_t)testblk_ro.logical_block_size);
    serial_write_string("\n  blocks: ");
    serial_write_u64(testblk_ro.block_count);
    serial_write_string("\n");

    if (block_device_read(&testblk_ro, 2ULL, 1U, before) !=
        BLOCK_DEVICE_RESULT_OK) {
        block_device_test_fail("read-only-data-unchanged");
    }
    block_test_fill_write_pattern(replacement, sizeof(replacement), 0x77U);

    if (block_device_write(&testblk_ro, 2ULL, 1U, replacement) !=
        BLOCK_DEVICE_RESULT_READ_ONLY) {
        block_device_test_fail("write-rejected");
    }
    serial_write_string("  write-rejected: PASS\n");

    if (testblk_ro_backend.write_calls != writes_before) {
        block_device_test_fail("backend-not-called");
    }
    serial_write_string("  backend-not-called: PASS\n");

    if ((block_device_read(&testblk_ro, 2ULL, 1U, after) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !block_test_buffer_equals(before, after, sizeof(before))) {
        block_device_test_fail("data-unchanged");
    }
    serial_write_string("  data-unchanged: PASS\n");
}

static void block_test_backend_failure(void) {
    uint8_t buffer[TESTBLK0_BLOCK_SIZE];
    uint64_t read_calls;
    uint64_t write_calls;

    serial_write_string("\nBackend error:\n");

    read_calls = testblk0_backend.read_calls;
    testblk0_backend.fail_read = true;
    if ((block_device_read(&testblk0, 5ULL, 1U, buffer) !=
         BLOCK_DEVICE_RESULT_IO_ERROR) ||
        (testblk0_backend.read_calls != (read_calls + 1ULL))) {
        block_device_test_fail("read-propagation");
    }
    testblk0_backend.fail_read = false;

    write_calls = testblk0_backend.write_calls;
    testblk0_backend.fail_write = true;
    block_test_fill_write_pattern(buffer, sizeof(buffer), 0x44U);
    if ((block_device_write(&testblk0, 6ULL, 1U, buffer) !=
         BLOCK_DEVICE_RESULT_IO_ERROR) ||
        (testblk0_backend.write_calls != (write_calls + 1ULL))) {
        block_device_test_fail("write-propagation");
    }
    testblk0_backend.fail_write = false;

    serial_write_string("  propagation: PASS\n");
}

static void block_test_registry_capacity(void) {
    size_t index;

    for (index = 0U; index < 6U; ++index) {
        if (block_device_register(&filler_devices[index]) !=
            BLOCK_DEVICE_RESULT_OK) {
            block_device_test_fail("bounded-capacity");
        }
    }

    if ((block_device_count() != (size_t)BLOCK_DEVICE_MAX_DEVICES) ||
        (block_device_register(&overflow_device) !=
         BLOCK_DEVICE_RESULT_REGISTRY_FULL) ||
        (block_device_count() != (size_t)BLOCK_DEVICE_MAX_DEVICES)) {
        block_device_test_fail("bounded-capacity");
    }

    serial_write_string("\nRegistry capacity:\n");
    serial_write_string("  max devices: ");
    serial_write_u64((uint64_t)BLOCK_DEVICE_MAX_DEVICES);
    serial_write_string("\n  bounded-capacity: PASS\n");
}

void block_device_test_run(void) {
    block_test_initialize_backend(&testblk0_backend, 0x21U);
    block_test_initialize_backend(&testblk_ro_backend, 0x91U);

    serial_write_string("Generic block-device layer:\n");
    block_test_registry_and_registration();
    block_test_device_io();
    block_test_bounds();
    block_test_read_only();
    block_test_backend_failure();
    block_test_registry_capacity();

    serial_write_string("\nBoringKernel block-device test passed.\n");
    x86_64_halt_forever();
}
