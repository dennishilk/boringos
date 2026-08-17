#include <stdbool.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/kernel.h>
#include <boring/pmm.h>
#include <boring/serial.h>
#include <boring/vmm.h>

#define VMM_TEST_PATTERN 0x424f52494e474f53ULL

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

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_hhdm_request limine_hhdm_request = {
    .id = BORING_LIMINE_HHDM_REQUEST_ID,
    .revision = 0ULL,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_paging_mode_request limine_paging_mode_request = {
    .id = BORING_LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 1ULL,
    .response = 0,
    .mode = BORING_LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = BORING_LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = BORING_LIMINE_PAGING_MODE_X86_64_4LVL
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

static bool vmm_self_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("VMM self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    return false;
}

static bool vmm_self_test(void) {
    struct pmm_stats pmm_before;
    struct pmm_stats pmm_after;
    struct vmm_stats vmm_before;
    struct vmm_stats vmm_after_map;
    struct vmm_stats vmm_after_unmap;
    const uintptr_t test_virtual = vmm_test_virtual_address();
    volatile uint64_t *const test_pointer =
        (volatile uint64_t *)test_virtual;
    uint64_t test_physical;
    uint64_t translated = 0ULL;
    uint64_t translation_result;
    uint64_t read_back;
    uint64_t page_table_frames;

    serial_write_string("VMM self-test:\n");

    if (!pmm_get_stats(&pmm_before) || !vmm_get_stats(&vmm_before) ||
        !pmm_alloc_frame(&test_physical)) {
        return vmm_self_test_fail("frame-allocation");
    }
    serial_write_string("  frame-allocation: PASS\n");

    if (vmm_translate(test_virtual, &translated)) {
        return vmm_self_test_fail("unmapped-check");
    }
    serial_write_string("  unmapped-check: PASS\n");

    if (!vmm_map_page(test_virtual, test_physical, VMM_FLAG_WRITABLE) ||
        !vmm_get_stats(&vmm_after_map) ||
        (vmm_after_map.owned_page_table_frames <
         vmm_before.owned_page_table_frames)) {
        return vmm_self_test_fail("map");
    }
    page_table_frames = vmm_after_map.owned_page_table_frames -
                        vmm_before.owned_page_table_frames;
    serial_write_string("  map: PASS\n");

    if (!vmm_translate(test_virtual, &translated) ||
        (translated != test_physical)) {
        return vmm_self_test_fail("translate");
    }
    translation_result = translated;
    serial_write_string("  translate: PASS\n");

    *test_pointer = VMM_TEST_PATTERN;
    read_back = *test_pointer;
    if (read_back != VMM_TEST_PATTERN) {
        return vmm_self_test_fail("write-read");
    }
    *test_pointer = 0ULL;
    serial_write_string("  write-read: PASS\n");

    if (!vmm_unmap_page(test_virtual) ||
        vmm_translate(test_virtual, &translated)) {
        return vmm_self_test_fail("unmap");
    }
    serial_write_string("  unmap: PASS\n");

    if (!vmm_get_stats(&vmm_after_unmap) ||
        (vmm_after_unmap.owned_page_table_frames !=
         vmm_before.owned_page_table_frames) ||
        !pmm_free_frame(test_physical) ||
        !pmm_get_stats(&pmm_after) ||
        (pmm_after.free_frames != pmm_before.free_frames) ||
        (pmm_after.usable_frames != pmm_before.usable_frames)) {
        return vmm_self_test_fail("frame-release");
    }
    serial_write_string("  frame-release: PASS\n");

    serial_write_string("Page-table frames allocated: ");
    serial_write_u64(page_table_frames);
    serial_write_string("\n");
    serial_write_string("Test physical frame: ");
    serial_write_u64(test_physical);
    serial_write_string("\n");
    serial_write_string("Translation result: ");
    serial_write_u64(translation_result);
    serial_write_string("\n");
    serial_write_string("Test pattern: 0x424F52494E474F53\n");

    return true;
}

void boring_kernel_entry(void) {
    struct pmm_stats pmm_stats;
    struct vmm_stats vmm_stats;

    if (!BORING_LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        x86_64_halt_forever();
    }

    serial_init();
    serial_write_string("BoringOS booting...\n");
    serial_write_string("BoringKernel 0.0.3-dev\n");
    serial_write_string("Arch: x86_64\n");
    serial_write_string("Hello from BoringKernel.\n\n");

    if (!pmm_init(limine_memmap_request.response) ||
        !pmm_get_stats(&pmm_stats)) {
        serial_write_string("Physical memory manager: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Physical memory manager:\n");
    serial_write_string("Page size: 4096 bytes\n");
    serial_write_string("Usable memory: ");
    serial_write_u64(pmm_stats.usable_bytes);
    serial_write_string(" bytes\n");
    serial_write_string("Usable frames: ");
    serial_write_u64(pmm_stats.usable_frames);
    serial_write_string("\n");
    serial_write_string("Usable regions: ");
    serial_write_u64(pmm_stats.region_count);
    serial_write_string("\n");
    serial_write_string("PMM: online\n\n");

    if (!pmm_self_test()) {
        x86_64_halt_forever();
    }
    serial_write_string("\nBoringKernel physical memory test passed.\n\n");

    if (!vmm_init(limine_hhdm_request.response,
                  limine_paging_mode_request.response,
                  limine_memmap_request.response) ||
        !vmm_get_stats(&vmm_stats)) {
        serial_write_string("Virtual memory manager: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Virtual memory manager:\n");
    serial_write_string("Paging: x86_64 4-level\n");
    serial_write_string("Page size: 4096 bytes\n");
    serial_write_string("Active root table: ");
    serial_write_u64(vmm_stats.active_root_physical);
    serial_write_string("\n");
    serial_write_string("HHDM offset: ");
    serial_write_u64(vmm_stats.hhdm_offset);
    serial_write_string("\n");
    serial_write_string("Test virtual address: ");
    serial_write_u64((uint64_t)vmm_test_virtual_address());
    serial_write_string("\n");
    serial_write_string("VMM: online\n\n");

    if (!vmm_self_test()) {
        x86_64_halt_forever();
    }

    serial_write_string("\nBoringKernel virtual memory test passed.\n");
    x86_64_halt_forever();
}
