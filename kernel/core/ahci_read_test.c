#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci.h>
#include <boring/ahci_read_test.h>
#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/serial.h>

#define M56_FIXTURE_SECTOR_SIZE 512U
#define M56_MULTI_START_LBA 123ULL
#define M56_MULTI_BLOCKS 3U

static uint8_t read_buffer[AHCI_M56_DMA_BYTES];

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M56 AHCI READ QEMU FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static uint8_t fixture_byte(uint64_t lba, uint32_t offset) {
    const uint64_t value = (lba * 17ULL) + ((uint64_t)offset * 13ULL) +
                           (lba >> 8U) + 0x5aULL;
    return (uint8_t)(value & 0xffULL);
}

static bool verify_pattern(const uint8_t *buffer, uint64_t first_block,
                           uint32_t block_count, uint32_t block_size) {
    uint32_t block;

    if ((buffer == NULL) || (block_size != M56_FIXTURE_SECTOR_SIZE)) {
        return false;
    }
    for (block = 0U; block < block_count; ++block) {
        uint32_t offset;
        const uint64_t lba = first_block + (uint64_t)block;
        const size_t base = (size_t)block * (size_t)block_size;
        for (offset = 0U; offset < block_size; ++offset) {
            if (buffer[base + (size_t)offset] != fixture_byte(lba, offset)) {
                return false;
            }
        }
    }
    return true;
}

static void read_and_verify(const struct block_device *device,
                            uint64_t first_block, uint32_t block_count,
                            const char *marker) {
    uint32_t bytes;

    if ((device == NULL) ||
        !ahci_compute_transfer_bytes(device->logical_block_size, block_count,
                                     AHCI_M56_DMA_BYTES, &bytes)) {
        fail("acceptance transfer bounds");
    }
    (void)bytes;
    if (block_device_read(device, first_block, block_count, read_buffer) !=
        BLOCK_DEVICE_RESULT_OK) {
        fail("real AHCI read");
    }
    if (!verify_pattern(read_buffer, first_block, block_count,
                        device->logical_block_size)) {
        fail("fixture pattern mismatch");
    }
    serial_write_string(marker);
    serial_write_string(": PASS\n");
}

void ahci_read_test_run(void) {
    struct ahci_state controller;
    struct ahci_block_stats stats;
    struct ahci_block_stats before_boundary;
    struct ahci_block_stats after_boundary;
    const struct block_device *device;
    enum ahci_block_result init_result;
    uint64_t middle;

    block_device_init();
    if (!ahci_init(&controller)) {
        fail("controller initialization");
    }
    if (!controller.initialized || (controller.sata_ports == 0U)) {
        fail("controller SATA state");
    }

    init_result = ahci_block_init();
    if (init_result != AHCI_BLOCK_RESULT_OK) {
        serial_write_string("M56 AHCI block init result: ");
        serial_write_string(ahci_block_result_name(init_result));
        serial_write_string("\n");
        fail("block initialization");
    }
    device = ahci_block_device();
    if ((device == NULL) || !ahci_block_get_stats(&stats) ||
        !stats.registered || !stats.read_only || !stats.lba48 ||
        !stats.bus_master_enabled || !stats.command_engine_started ||
        (stats.identify_commands != 1ULL) ||
        (stats.logical_blocks != device->block_count) ||
        (stats.logical_block_size != device->logical_block_size) ||
        (stats.logical_block_size != M56_FIXTURE_SECTOR_SIZE) ||
        (stats.max_blocks_per_command < M56_MULTI_BLOCKS)) {
        fail("IDENTIFY geometry or registration state");
    }
    if ((block_device_count() != 1U) ||
        (block_device_find("ahci0") != device) || !device->read_only) {
        fail("generic block registration");
    }

    serial_write_string("M56 AHCI IDENTIFY: port=");
    serial_write_u64((uint64_t)stats.port);
    serial_write_string(" commands=");
    serial_write_u64(stats.identify_commands);
    serial_write_string(" LBA48=");
    serial_write_u64(stats.lba48 ? 1ULL : 0ULL);
    serial_write_string("\nM56 AHCI geometry: blocks=");
    serial_write_u64(stats.logical_blocks);
    serial_write_string(" logical_block_size=");
    serial_write_u64((uint64_t)stats.logical_block_size);
    serial_write_string(" max_blocks_per_command=");
    serial_write_u64((uint64_t)stats.max_blocks_per_command);
    serial_write_string("\nM56 AHCI DMA frames: ");
    serial_write_u64((uint64_t)stats.dma_frame_count);
    serial_write_string("\nM56 AHCI generic block registration: PASS\n");

    read_and_verify(device, 0ULL, 1U, "M56 AHCI first LBA");
    middle = device->block_count / 2ULL;
    read_and_verify(device, middle, 1U, "M56 AHCI middle LBA");
    read_and_verify(device, device->block_count - 1ULL, 1U,
                    "M56 AHCI last LBA");
    read_and_verify(device, M56_MULTI_START_LBA, M56_MULTI_BLOCKS,
                    "M56 AHCI multi-sector read");
    if ((middle == 0ULL) || (middle >= (device->block_count - 1ULL))) {
        fail("middle neighbor range");
    }
    read_and_verify(device, middle - 1ULL, M56_MULTI_BLOCKS,
                    "M56 AHCI neighboring sectors");

    if (!ahci_block_get_stats(&before_boundary)) {
        fail("pre-boundary stats");
    }
    if (block_device_read(device, device->block_count, 1U, read_buffer) !=
        BLOCK_DEVICE_RESULT_OUT_OF_RANGE) {
        fail("out-of-range result");
    }
    if (!ahci_block_get_stats(&after_boundary) ||
        (after_boundary.read_commands != before_boundary.read_commands)) {
        fail("out-of-range touched hardware");
    }
    serial_write_string("M56 AHCI out-of-range pre-I/O: PASS\n");

    if (block_device_write(device, 0ULL, 1U, read_buffer) !=
        BLOCK_DEVICE_RESULT_READ_ONLY) {
        fail("read-only write rejection");
    }
    serial_write_string("M56 AHCI read-only write rejection: PASS\n");

    if (!ahci_block_get_stats(&stats) || (stats.read_commands < 7ULL)) {
        fail("read command evidence");
    }
    serial_write_string("M56 AHCI READ DMA EXT commands: ");
    serial_write_u64(stats.read_commands);
    serial_write_string("\nM56 AHCI synchronous read-only block path QEMU passed.\n");
    x86_64_halt_forever();
}
