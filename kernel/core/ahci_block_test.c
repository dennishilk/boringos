#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci.h>
#include <boring/ahci_block.h>
#include <boring/ahci_block_test.h>
#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/serial.h>

static uint8_t read_buffer[AHCI_BLOCK_DMA_BYTES];

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M56 AHCI READ QEMU FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static uint8_t expected_byte(uint64_t block, uint32_t offset) {
    return (uint8_t)((block + (uint64_t)offset) & 0xffULL);
}

static bool verify_read(const struct block_device *device,
                        uint64_t first_block, uint32_t block_count) {
    uint32_t byte_count;
    size_t index;

    if ((device == NULL) ||
        !ahci_dma_transfer_bytes(device->logical_block_size,
                                 block_count, &byte_count) ||
        block_device_read(device, first_block, block_count,
                          read_buffer) != BLOCK_DEVICE_RESULT_OK) {
        return false;
    }

    for (index = 0U; index < (size_t)byte_count; ++index) {
        const uint64_t relative_block =
            (uint64_t)(index / (size_t)device->logical_block_size);
        const uint32_t block_offset =
            (uint32_t)(index % (size_t)device->logical_block_size);
        if (read_buffer[index] !=
            expected_byte(first_block + relative_block, block_offset)) {
            return false;
        }
    }
    return true;
}

void ahci_block_test_run(void) {
    struct ahci_state controller;
    struct ahci_block_stats stats;
    const struct block_device *device;
    enum ahci_block_result result;
    uint64_t middle;
    uint32_t ignored_bytes;

    block_device_init();
    if (!ahci_init(&controller)) {
        fail("M55 controller foundation init");
    }

    result = ahci_block_init(&controller);
    if (result != AHCI_BLOCK_RESULT_OK) {
        serial_write_string("M56 AHCI block init result: ");
        serial_write_string(ahci_block_result_name(result));
        serial_write_string("\n");
        fail("read-only block init");
    }
    if (!ahci_block_get_stats(&stats) || !stats.identify_complete ||
        !stats.registered || (stats.capacity_blocks == 0ULL) ||
        (stats.logical_block_size != 512U) ||
        (stats.max_blocks_per_transfer != 8U)) {
        fail("IDENTIFY geometry");
    }

    device = ahci_block_device();
    if ((device == NULL) || (device != block_device_find(AHCI_BLOCK_DEVICE_NAME)) ||
        (block_device_count() != 1U) || device->read_only ||
        (device->logical_block_size != stats.logical_block_size) ||
        (device->block_count != stats.capacity_blocks)) {
        fail("M21 registration");
    }

    serial_write_string("M56 AHCI IDENTIFY: PASS\n");
    serial_write_string("M56 AHCI port: ");
    serial_write_u64((uint64_t)stats.port);
    serial_write_string("\nM56 AHCI capacity blocks: ");
    serial_write_u64(stats.capacity_blocks);
    serial_write_string("\nM56 AHCI logical sector size: ");
    serial_write_u64((uint64_t)stats.logical_block_size);
    serial_write_string("\nM56 AHCI LBA48: ");
    serial_write_string(stats.lba48 ? "yes\n" : "no\n");
    serial_write_string("M56 AHCI max blocks per transfer: ");
    serial_write_u64((uint64_t)stats.max_blocks_per_transfer);
    serial_write_string("\nM56 M21 read path preserved on sata0\n");

    if (!verify_read(device, 0ULL, 1U)) {
        fail("first valid region");
    }
    serial_write_string("M56 first read: PASS\n");

    middle = stats.capacity_blocks / 2ULL;
    if (!verify_read(device, middle, 1U)) {
        fail("middle region");
    }
    serial_write_string("M56 middle read: PASS\n");

    if (!verify_read(device, stats.capacity_blocks - 1ULL, 1U)) {
        fail("last valid region");
    }
    serial_write_string("M56 last read: PASS\n");

    if ((stats.capacity_blocks <= 1238ULL) ||
        !verify_read(device, 1234ULL, 4U)) {
        fail("multi-sector region");
    }
    serial_write_string("M56 multi-sector read: PASS\n");

    if (block_device_read(device, stats.capacity_blocks - 1ULL, 2U,
                          read_buffer) != BLOCK_DEVICE_RESULT_OUT_OF_RANGE) {
        fail("out-of-range rejection");
    }
    serial_write_string("M56 out-of-range rejection: PASS\n");

    if (ahci_dma_transfer_bytes(stats.logical_block_size,
                                stats.max_blocks_per_transfer + 1U,
                                &ignored_bytes) ||
        block_device_read(device, 64ULL,
                          stats.max_blocks_per_transfer + 1U,
                          read_buffer) != BLOCK_DEVICE_RESULT_IO_ERROR) {
        fail("PRDT transfer bound");
    }
    serial_write_string("M56 PRDT transfer bound: PASS\n");

    if (!ahci_block_get_stats(&stats) ||
        (stats.reads_completed != 4ULL) ||
        (stats.commands_completed != 5ULL)) {
        fail("completion accounting");
    }
    serial_write_string("M56 real read completions: ");
    serial_write_u64(stats.reads_completed);
    serial_write_string("\nM56 AHCI synchronous read-only block path QEMU passed.\n");
    x86_64_halt_forever();
}
