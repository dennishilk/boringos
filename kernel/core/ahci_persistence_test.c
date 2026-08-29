#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci.h>
#include <boring/ahci_block.h>
#include <boring/ahci_persistence_test.h>
#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/serial.h>

#define M57_TEST_LBA 4096ULL
#define M57_TEST_BLOCKS 4U

static uint8_t buffer[AHCI_BLOCK_DMA_BYTES];

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M57 AHCI PERSISTENCE FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static uint8_t persisted_byte(size_t index) {
    return (uint8_t)(0xa5U ^ (uint8_t)(index & 0xffU));
}

static bool buffer_is_persisted(size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (buffer[index] != persisted_byte(index)) {
            return false;
        }
    }
    return true;
}

void ahci_persistence_test_run(void) {
    struct ahci_state controller;
    struct ahci_block_stats stats;
    const struct block_device *device;
    uint32_t bytes;
    size_t index;

    block_device_init();
    if (!ahci_init(&controller) ||
        (ahci_block_init(&controller) != AHCI_BLOCK_RESULT_OK)) {
        fail("controller/block init");
    }
    device = ahci_block_device();
    if ((device == NULL) || (device != block_device_find("sata0")) ||
        device->read_only || (device->logical_block_size != 512U) ||
        !ahci_dma_transfer_bytes(device->logical_block_size,
                                 M57_TEST_BLOCKS, &bytes) ||
        (bytes != 2048U) ||
        (M57_TEST_LBA + M57_TEST_BLOCKS > device->block_count)) {
        fail("writable M21 geometry");
    }
    if (block_device_read(device, M57_TEST_LBA, M57_TEST_BLOCKS,
                          buffer) != BLOCK_DEVICE_RESULT_OK) {
        fail("pre-write read");
    }

    if (buffer_is_persisted((size_t)bytes)) {
        if (!ahci_block_get_stats(&stats) ||
            (stats.reads_completed != 1ULL) ||
            (stats.writes_completed != 0ULL) ||
            (stats.flushes_completed != 0ULL)) {
            fail("reboot accounting");
        }
        serial_write_string("M57 AHCI reboot persistence read: PASS\n");
        serial_write_string("M57 AHCI second boot performed no write: PASS\n");
        serial_write_string("M57 AHCI writable persistence QEMU passed.\n");
        x86_64_halt_forever();
    }

    for (index = 0U; index < (size_t)bytes; ++index) {
        buffer[index] = persisted_byte(index);
    }
    if (block_device_write(device, M57_TEST_LBA, M57_TEST_BLOCKS,
                           buffer) != BLOCK_DEVICE_RESULT_OK) {
        fail("WRITE DMA completion/flush");
    }
    for (index = 0U; index < (size_t)bytes; ++index) {
        buffer[index] = 0U;
    }
    if ((block_device_read(device, M57_TEST_LBA, M57_TEST_BLOCKS,
                           buffer) != BLOCK_DEVICE_RESULT_OK) ||
        !buffer_is_persisted((size_t)bytes) ||
        !ahci_block_get_stats(&stats) ||
        (stats.writes_completed != 1ULL) ||
        (stats.reads_completed != 2ULL) ||
        !stats.write_cache_supported || !stats.write_cache_enabled ||
        (stats.flushes_completed != 1ULL)) {
        fail("write/read/flush evidence");
    }
    serial_write_string("M57 AHCI WRITE DMA completion: PASS\n");
    serial_write_string("M57 AHCI FLUSH CACHE completion: PASS\n");
    serial_write_string("M57 AHCI immediate readback: PASS\n");
    serial_write_string("M57 AHCI first boot write complete.\n");
    x86_64_halt_forever();
}
