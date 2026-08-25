#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/serial.h>
#include <boring/virtio_blk.h>
#include <boring/virtio_blk_test.h>

#define TEST_KNOWN_LBA 7ULL
#define TEST_SINGLE_LBA 32ULL
#define TEST_MULTI_FIRST_LBA 40ULL
#define TEST_MULTI_COUNT 4U
#define TEST_CHUNK_FIRST_LBA 64ULL
#define TEST_CHUNK_COUNT 12U
#define TEST_LEFT_NEIGHBOR (TEST_MULTI_FIRST_LBA - 1ULL)
#define TEST_RIGHT_NEIGHBOR (TEST_MULTI_FIRST_LBA + (uint64_t)TEST_MULTI_COUNT)
#define TEST_SECTOR_SIZE 512U
#define TEST_MULTI_BYTES ((size_t)TEST_MULTI_COUNT * (size_t)TEST_SECTOR_SIZE)
#define TEST_CHUNK_BYTES ((size_t)TEST_CHUNK_COUNT * (size_t)TEST_SECTOR_SIZE)

static uint8_t test_single_write[TEST_SECTOR_SIZE];
static uint8_t test_single_read[TEST_SECTOR_SIZE];
static uint8_t test_multi_write[TEST_MULTI_BYTES];
static uint8_t test_multi_read[TEST_MULTI_BYTES];
static uint8_t test_chunk_write[TEST_CHUNK_BYTES];
static uint8_t test_chunk_read[TEST_CHUNK_BYTES];
static uint8_t test_scratch[TEST_SECTOR_SIZE];
static uint8_t test_left_before[TEST_SECTOR_SIZE];
static uint8_t test_right_before[TEST_SECTOR_SIZE];
static uint8_t test_neighbor_after[TEST_SECTOR_SIZE];

static void test_fail(const char *check) __attribute__((noreturn));
static void test_fail(const char *check) {
    serial_write_string("VirtIO block test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static char test_hex_digit(uint8_t value) {
    return (value < 10U) ? (char)('0' + value) :
                           (char)('A' + (value - 10U));
}

static void test_write_hex8(uint8_t value) {
    char text[2];

    text[0] = test_hex_digit((uint8_t)(value >> 4U));
    text[1] = test_hex_digit((uint8_t)(value & 0x0fU));
    serial_write_bytes(text, sizeof(text));
}

static void test_write_hex16(uint16_t value) {
    char text[6];

    text[0] = '0';
    text[1] = 'x';
    text[2] = test_hex_digit((uint8_t)((value >> 12U) & 0x0fU));
    text[3] = test_hex_digit((uint8_t)((value >> 8U) & 0x0fU));
    text[4] = test_hex_digit((uint8_t)((value >> 4U) & 0x0fU));
    text[5] = test_hex_digit((uint8_t)(value & 0x0fU));
    serial_write_bytes(text, sizeof(text));
}

static void test_write_bdf(const struct virtio_blk_stats *stats) {
    serial_write_string("  BDF: ");
    test_write_hex8(stats->pci_bus);
    serial_write_string(":");
    test_write_hex8(stats->pci_device);
    serial_write_string(".");
    {
        char function_text[1];
        function_text[0] = test_hex_digit(stats->pci_function);
        serial_write_bytes(function_text, sizeof(function_text));
    }
    serial_write_string("\n");
}

static uint8_t test_initial_byte(uint64_t lba, size_t offset) {
    const uint64_t value = ((lba & 0xffULL) * 17ULL) +
                           (((uint64_t)offset & 0xffULL) * 31ULL) +
                           0x5aULL;
    return (uint8_t)(value & 0xffULL);
}

static uint8_t test_single_byte(size_t offset) {
    const size_t value = 0xa5U + ((offset & 0xffU) * 13U);
    return (uint8_t)(value & 0xffU);
}

static uint8_t test_multi_byte(uint32_t relative_sector, size_t offset) {
    const size_t value = 0xc3U + ((size_t)relative_sector * 29U) +
                         ((offset & 0xffU) * 7U);
    return (uint8_t)(value & 0xffU);
}

static void test_fill_initial(uint8_t *buffer, uint64_t lba) {
    size_t index;

    for (index = 0U; index < (size_t)TEST_SECTOR_SIZE; ++index) {
        buffer[index] = test_initial_byte(lba, index);
    }
}

static void test_fill_single_write(void) {
    size_t index;

    for (index = 0U; index < (size_t)TEST_SECTOR_SIZE; ++index) {
        test_single_write[index] = test_single_byte(index);
    }
}

static void test_fill_multi_write(void) {
    uint32_t sector;

    for (sector = 0U; sector < TEST_MULTI_COUNT; ++sector) {
        size_t offset;
        const size_t base = (size_t)sector * (size_t)TEST_SECTOR_SIZE;
        for (offset = 0U; offset < (size_t)TEST_SECTOR_SIZE; ++offset) {
            test_multi_write[base + offset] = test_multi_byte(sector, offset);
        }
    }
}

static void test_fill_chunk_write(void) {
    uint32_t sector;

    for (sector = 0U; sector < TEST_CHUNK_COUNT; ++sector) {
        size_t offset;
        const size_t base = (size_t)sector * (size_t)TEST_SECTOR_SIZE;
        for (offset = 0U; offset < (size_t)TEST_SECTOR_SIZE; ++offset) {
            test_chunk_write[base + offset] =
                (uint8_t)((0x6dU + (sector * 19U) +
                           ((uint32_t)offset * 11U)) & 0xffU);
        }
    }
}

static bool test_bytes_equal(const uint8_t *left,
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

static void test_expect_initial_read(const struct block_device *device,
                                     uint64_t lba,
                                     const char *check) {
    uint8_t expected[TEST_SECTOR_SIZE];

    test_fill_initial(expected, lba);
    if ((block_device_read(device, lba, 1U, test_scratch) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !test_bytes_equal(test_scratch, expected, sizeof(expected))) {
        test_fail(check);
    }
}

static void test_print_discovery(const struct virtio_blk_stats *stats) {
    if (!stats->pci_discovered || !stats->memory_space_bus_master ||
        (stats->pci_vendor_id != 0x1af4U) ||
        (stats->pci_device_id != 0x1042U)) {
        test_fail("pci-discovery");
    }

    serial_write_string("Modern VirtIO block:\n\nPCI:\n");
    serial_write_string("  discovery: PASS\n");
    test_write_bdf(stats);
    serial_write_string("  transport: modern PCI\n  vendor: ");
    test_write_hex16(stats->pci_vendor_id);
    serial_write_string("\n  device: ");
    test_write_hex16(stats->pci_device_id);
    serial_write_string("\n  memory-space: PASS\n  bus-master: PASS\n\n");

    if (!stats->common_config_found || !stats->notify_config_found ||
        !stats->device_config_found) {
        test_fail("capabilities");
    }

    serial_write_string("Capabilities:\n");
    serial_write_string("  common-config: PASS\n");
    serial_write_string("  notify-config: PASS\n");
    serial_write_string("  device-config: PASS\n");
    serial_write_string("  common BAR: ");
    serial_write_u64((uint64_t)stats->common_bar);
    serial_write_string("\n  notify BAR: ");
    serial_write_u64((uint64_t)stats->notify_bar);
    serial_write_string("\n  device BAR: ");
    serial_write_u64((uint64_t)stats->device_bar);
    serial_write_string("\n\n");
}

static void test_print_negotiation(const struct virtio_blk_stats *stats) {
    if (!stats->reset_complete || !stats->version_1_offered ||
        !stats->features_ok || !stats->queue_enabled || !stats->driver_ok ||
        (stats->queue_size != VIRTIO_BLK_QUEUE_SIZE) ||
        (stats->dma_frame_count != VIRTIO_BLK_DMA_FRAME_COUNT)) {
        test_fail("negotiation");
    }

    serial_write_string("Negotiation:\n");
    serial_write_string("  reset: PASS\n");
    serial_write_string("  VERSION_1: PASS\n");
    serial_write_string("  device features: ");
    serial_write_hex_u64(stats->device_features);
    serial_write_string("\n  accepted features: ");
    serial_write_hex_u64(stats->accepted_features);
    serial_write_string("\n  FEATURES_OK: PASS\n");
    serial_write_string("  queue-size: ");
    serial_write_u64((uint64_t)stats->queue_size);
    serial_write_string("\n  DMA frames: ");
    serial_write_u64((uint64_t)stats->dma_frame_count);
    serial_write_string("\n  queue-enable: PASS\n  DRIVER_OK: PASS\n\n");
}

static void test_print_block_device(const struct block_device *device) {
    uint64_t capacity_bytes;

    if ((device == NULL) || (device->logical_block_size != TEST_SECTOR_SIZE) ||
        (device->block_count == 0ULL) ||
        (device->block_count > (UINT64_MAX / TEST_SECTOR_SIZE)) ||
        (device->block_count <= TEST_RIGHT_NEIGHBOR)) {
        test_fail("block-device-geometry");
    }
    capacity_bytes = device->block_count * TEST_SECTOR_SIZE;

    serial_write_string("Block device:\n  name: vblk0\n");
    serial_write_string("  logical block size: ");
    serial_write_u64((uint64_t)device->logical_block_size);
    serial_write_string("\n  sectors: ");
    serial_write_u64(device->block_count);
    serial_write_string("\n  capacity: ");
    serial_write_u64(capacity_bytes);
    serial_write_string(" bytes\n  registration: PASS\n\n");
}

void virtio_blk_test_run(void) {
    enum virtio_blk_result init_result;
    const struct block_device *device;
    struct virtio_blk_stats stats_before_bounds;
    struct virtio_blk_stats stats_after;
    uint8_t expected[TEST_SECTOR_SIZE];

    block_device_init();
    init_result = virtio_blk_init();
    if (init_result != VIRTIO_BLK_RESULT_OK) {
        serial_write_string("VirtIO block init result: ");
        serial_write_string(virtio_blk_result_name(init_result));
        serial_write_string("\n");
        test_fail("driver-init");
    }

    device = block_device_find("vblk0");
    if ((device == NULL) || (device != virtio_blk_device()) ||
        (block_device_count() != 1U) || device->read_only) {
        test_fail("m21-registration");
    }

    if (!virtio_blk_get_stats(&stats_after)) {
        test_fail("stats");
    }
    test_print_discovery(&stats_after);
    test_print_negotiation(&stats_after);
    test_print_block_device(device);

    serial_write_string("I/O:\n");

    test_fill_initial(expected, TEST_KNOWN_LBA);
    if ((block_device_read(device, TEST_KNOWN_LBA, 1U, test_scratch) !=
         BLOCK_DEVICE_RESULT_OK) ||
        !test_bytes_equal(test_scratch, expected, sizeof(expected))) {
        test_fail("known-sector-read");
    }
    serial_write_string("  known-sector-read: PASS\n");

    test_expect_initial_read(device, 0ULL, "first-sector-read");
    serial_write_string("  first-sector-read: PASS\n");

    test_expect_initial_read(device, device->block_count - 1ULL,
                             "last-sector-read");
    serial_write_string("  last-sector-read: PASS\n");

    if ((block_device_read(device, TEST_LEFT_NEIGHBOR, 1U,
                           test_left_before) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(device, TEST_RIGHT_NEIGHBOR, 1U,
                           test_right_before) != BLOCK_DEVICE_RESULT_OK)) {
        test_fail("neighbor-baseline");
    }
    test_fill_initial(expected, TEST_LEFT_NEIGHBOR);
    if (!test_bytes_equal(test_left_before, expected, sizeof(expected))) {
        test_fail("left-neighbor-baseline");
    }
    test_fill_initial(expected, TEST_RIGHT_NEIGHBOR);
    if (!test_bytes_equal(test_right_before, expected, sizeof(expected))) {
        test_fail("right-neighbor-baseline");
    }

    test_fill_single_write();
    if (block_device_write(device, TEST_SINGLE_LBA, 1U,
                           test_single_write) != BLOCK_DEVICE_RESULT_OK) {
        test_fail("single-sector-write");
    }
    serial_write_string("  single-sector-write: PASS\n");

    if ((block_device_read(device, TEST_SINGLE_LBA, 1U,
                           test_single_read) != BLOCK_DEVICE_RESULT_OK) ||
        !test_bytes_equal(test_single_read, test_single_write,
                          sizeof(test_single_read))) {
        test_fail("single-sector-read-back");
    }
    serial_write_string("  single-sector-read-back: PASS\n");

    test_fill_multi_write();
    if ((block_device_write(device, TEST_MULTI_FIRST_LBA, TEST_MULTI_COUNT,
                            test_multi_write) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(device, TEST_MULTI_FIRST_LBA, TEST_MULTI_COUNT,
                           test_multi_read) != BLOCK_DEVICE_RESULT_OK) ||
        !test_bytes_equal(test_multi_read, test_multi_write,
                          sizeof(test_multi_read))) {
        test_fail("multi-sector-write-read-back");
    }
    serial_write_string("  multi-sector-write-read-back: PASS\n");

    test_fill_chunk_write();
    if (!virtio_blk_get_stats(&stats_before_bounds)) {
        test_fail("chunking-stats-before");
    }
    if ((block_device_write(device, TEST_CHUNK_FIRST_LBA, TEST_CHUNK_COUNT,
                            test_chunk_write) != BLOCK_DEVICE_RESULT_OK) ||
        (block_device_read(device, TEST_CHUNK_FIRST_LBA, TEST_CHUNK_COUNT,
                           test_chunk_read) != BLOCK_DEVICE_RESULT_OK) ||
        !test_bytes_equal(test_chunk_read, test_chunk_write,
                          sizeof(test_chunk_read)) ||
        !virtio_blk_get_stats(&stats_after) ||
        ((stats_after.submissions - stats_before_bounds.submissions) != 4ULL)) {
        test_fail("multi-request-chunking");
    }
    serial_write_string("  multi-request-chunking: PASS\n");

    if ((block_device_read(device, TEST_LEFT_NEIGHBOR, 1U,
                           test_neighbor_after) != BLOCK_DEVICE_RESULT_OK) ||
        !test_bytes_equal(test_neighbor_after, test_left_before,
                          sizeof(test_neighbor_after)) ||
        (block_device_read(device, TEST_RIGHT_NEIGHBOR, 1U,
                           test_neighbor_after) != BLOCK_DEVICE_RESULT_OK) ||
        !test_bytes_equal(test_neighbor_after, test_right_before,
                          sizeof(test_neighbor_after))) {
        test_fail("neighbor-preservation");
    }
    serial_write_string("  neighbor-preservation: PASS\n");

    if (!virtio_blk_get_stats(&stats_before_bounds)) {
        test_fail("stats-before-bounds");
    }
    if ((block_device_read(device, device->block_count, 1U, test_scratch) !=
         BLOCK_DEVICE_RESULT_OUT_OF_RANGE) ||
        (block_device_read(device, device->block_count - 1ULL, 2U,
                           test_scratch) != BLOCK_DEVICE_RESULT_OUT_OF_RANGE) ||
        (block_device_read(device, 0ULL, 0U, test_scratch) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        (block_device_read(device, 0ULL, 1U, NULL) !=
         BLOCK_DEVICE_RESULT_INVALID_ARGUMENT) ||
        !virtio_blk_get_stats(&stats_after) ||
        (stats_after.submissions != stats_before_bounds.submissions)) {
        test_fail("rejected-range-no-submit");
    }
    serial_write_string("  rejected-range-no-submit: PASS\n\n");

    if ((stats_after.submissions <= (uint64_t)stats_after.queue_size) ||
        (stats_after.completions != stats_after.submissions)) {
        test_fail("used-ring-completion");
    }
    serial_write_string("VirtIO:\n");
    serial_write_string("  used-ring completion: PASS\n");
    serial_write_string("  request status: PASS\n");
    serial_write_string("  submissions: ");
    serial_write_u64(stats_after.submissions);
    serial_write_string("\n  completions: ");
    serial_write_u64(stats_after.completions);
    serial_write_string("\n  polling spin limit: ");
    serial_write_u64((uint64_t)stats_after.poll_spin_limit);
    serial_write_string("\n\nBoringKernel VirtIO block test passed.\n");

    x86_64_halt_forever();
}
