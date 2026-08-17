#include <stdbool.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/kernel.h>
#include <boring/pmm.h>
#include <boring/serial.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = BORING_LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = BORING_LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_memmap_request limine_memmap_request = {
    .id = BORING_LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0ULL,
    .response = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = BORING_LIMINE_REQUESTS_END_MARKER;

static bool pmm_self_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("PMM self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    return false;
}

static bool pmm_self_test(void) {
    enum { TEST_FRAME_COUNT = 4 };
    uint64_t frames[TEST_FRAME_COUNT];
    uint64_t replacement;
    uint64_t first_index;
    uint64_t second_index;
    struct pmm_stats before;
    struct pmm_stats after;

    serial_write_string("PMM self-test:\n");

    if (!pmm_get_stats(&before) ||
        (before.free_frames < (uint64_t)TEST_FRAME_COUNT)) {
        return pmm_self_test_fail("allocate");
    }

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        if (!pmm_alloc_frame(&frames[first_index])) {
            return pmm_self_test_fail("allocate");
        }
    }
    serial_write_string("  allocate: PASS\n");

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        for (second_index = first_index + 1ULL;
             second_index < (uint64_t)TEST_FRAME_COUNT; ++second_index) {
            if (frames[first_index] == frames[second_index]) {
                return pmm_self_test_fail("unique");
            }
        }
    }
    serial_write_string("  unique: PASS\n");

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        if ((frames[first_index] % PMM_PAGE_SIZE) != 0ULL) {
            return pmm_self_test_fail("aligned");
        }
    }
    serial_write_string("  aligned: PASS\n");

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        if (!pmm_frame_is_usable(frames[first_index])) {
            return pmm_self_test_fail("usable");
        }
    }
    serial_write_string("  usable: PASS\n");

    if (!pmm_get_stats(&after) ||
        (after.free_frames !=
         (before.free_frames - (uint64_t)TEST_FRAME_COUNT))) {
        return pmm_self_test_fail("bookkeeping");
    }

    if (!pmm_free_frame(frames[1]) ||
        !pmm_alloc_frame(&replacement) ||
        (replacement != frames[1])) {
        return pmm_self_test_fail("free");
    }
    serial_write_string("  free: PASS\n");

    if (!pmm_free_frame(replacement) ||
        pmm_free_frame(replacement) ||
        !pmm_free_frame(frames[0]) ||
        !pmm_free_frame(frames[2]) ||
        !pmm_free_frame(frames[3])) {
        return pmm_self_test_fail("invalid-free");
    }
    serial_write_string("  invalid-free: PASS\n");

    if (!pmm_get_stats(&after) ||
        (after.free_frames != before.free_frames) ||
        (after.usable_frames != before.usable_frames)) {
        return pmm_self_test_fail("bookkeeping");
    }
    serial_write_string("  bookkeeping: PASS\n");

    return true;
}

void boring_kernel_entry(void) {
    struct pmm_stats stats;

    if (!BORING_LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        x86_64_halt_forever();
    }

    serial_init();
    serial_write_string("BoringOS booting...\n");
    serial_write_string("BoringKernel 0.0.2-dev\n");
    serial_write_string("Arch: x86_64\n");
    serial_write_string("Hello from BoringKernel.\n\n");

    if (!pmm_init(limine_memmap_request.response) || !pmm_get_stats(&stats)) {
        serial_write_string("Physical memory manager: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Physical memory manager:\n");
    serial_write_string("Page size: 4096 bytes\n");
    serial_write_string("Usable memory: ");
    serial_write_u64(stats.usable_bytes);
    serial_write_string(" bytes\n");
    serial_write_string("Usable frames: ");
    serial_write_u64(stats.usable_frames);
    serial_write_string("\n");
    serial_write_string("Usable regions: ");
    serial_write_u64(stats.region_count);
    serial_write_string("\n");
    serial_write_string("PMM: online\n\n");

    if (!pmm_self_test()) {
        x86_64_halt_forever();
    }

    serial_write_string("\nBoringKernel physical memory test passed.\n");
    x86_64_halt_forever();
}
