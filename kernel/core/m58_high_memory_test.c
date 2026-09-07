#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/m58_high_memory_test.h>
#include <boring/pmm.h>
#include <boring/serial.h>
#include <boring/vmm.h>

#define M58_FOUR_GIB 0x100000000ULL
#define M58_PATTERN_HIGH_FIRST 0x4d35384849474831ULL
#define M58_PATTERN_HIGH_LAST 0x4d35384849474832ULL
#define M58_PATTERN_NEIGHBOR_FIRST 0x4d35384e45494731ULL
#define M58_PATTERN_NEIGHBOR_LAST 0x4d35384e45494732ULL

static void m58_fail(const char *reason) __attribute__((noreturn));
static void m58_memory_barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

static void m58_fail(const char *reason) {
    serial_write_string("M58 HIGH MEMORY TEST FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

void m58_high_memory_test_run(void) {
    struct pmm_stats before;
    struct pmm_stats allocated;
    struct pmm_stats after;
    uint64_t high_physical = 0ULL;
    uint64_t neighbor_physical = 0ULL;
    uint64_t neighbor_limit;
    void *high_virtual = NULL;
    void *neighbor_virtual = NULL;
    volatile uint64_t *high;
    volatile uint64_t *neighbor;
    const size_t last_word =
        ((size_t)PMM_PAGE_SIZE / sizeof(uint64_t)) - 1U;
    size_t index;

    if (!pmm_get_stats(&before)) {
        m58_fail("stats unavailable");
    }
    serial_write_string("M58 PMM usable bytes: ");
    serial_write_u64(before.usable_bytes);
    serial_write_string("\nM58 PMM usable frames: ");
    serial_write_u64(before.usable_frames);
    serial_write_string("\n");
    if ((before.usable_bytes <= M58_FOUR_GIB) || before.memory_map_capped) {
        m58_fail("32 GiB PMM map not fully managed");
    }

    if (!pmm_alloc_frame_in_range(M58_FOUR_GIB, UINT64_MAX,
                                  &high_physical) ||
        (high_physical < M58_FOUR_GIB) ||
        !pmm_frame_is_usable(high_physical)) {
        m58_fail("no managed frame at or above 4 GiB");
    }
    if (high_physical > (UINT64_MAX - (2ULL * PMM_PAGE_SIZE))) {
        m58_fail("high frame neighbor overflows");
    }
    neighbor_physical = high_physical + PMM_PAGE_SIZE;
    neighbor_limit = neighbor_physical + PMM_PAGE_SIZE;
    if (!pmm_frame_is_usable(neighbor_physical) ||
        !pmm_alloc_frame_in_range(neighbor_physical, neighbor_limit,
                                  &neighbor_physical) ||
        (neighbor_physical != high_physical + PMM_PAGE_SIZE)) {
        (void)pmm_free_frame(high_physical);
        m58_fail("adjacent high frame unavailable");
    }
    if (!pmm_get_stats(&allocated) ||
        (allocated.free_frames + 2ULL != before.free_frames)) {
        (void)pmm_free_frame(neighbor_physical);
        (void)pmm_free_frame(high_physical);
        m58_fail("allocation accounting");
    }
    if (!vmm_pmm_frame_to_hhdm(high_physical, &high_virtual) ||
        !vmm_pmm_frame_to_hhdm(neighbor_physical, &neighbor_virtual)) {
        (void)pmm_free_frame(neighbor_physical);
        (void)pmm_free_frame(high_physical);
        m58_fail("HHDM mapping");
    }

    high = (volatile uint64_t *)high_virtual;
    neighbor = (volatile uint64_t *)neighbor_virtual;
    high[0] = M58_PATTERN_HIGH_FIRST;
    high[last_word] = M58_PATTERN_HIGH_LAST;
    neighbor[0] = M58_PATTERN_NEIGHBOR_FIRST;
    neighbor[last_word] = M58_PATTERN_NEIGHBOR_LAST;
    m58_memory_barrier();
    if ((high[0] != M58_PATTERN_HIGH_FIRST) ||
        (high[last_word] != M58_PATTERN_HIGH_LAST) ||
        (neighbor[0] != M58_PATTERN_NEIGHBOR_FIRST) ||
        (neighbor[last_word] != M58_PATTERN_NEIGHBOR_LAST)) {
        m58_fail("high-frame write/read");
    }

    high[0] = ~M58_PATTERN_HIGH_FIRST;
    m58_memory_barrier();
    if ((high[0] != ~M58_PATTERN_HIGH_FIRST) ||
        (neighbor[0] != M58_PATTERN_NEIGHBOR_FIRST) ||
        (neighbor[last_word] != M58_PATTERN_NEIGHBOR_LAST)) {
        m58_fail("neighbor corruption");
    }

    serial_write_string("M58 high frame: ");
    serial_write_hex_u64(high_physical);
    serial_write_string("\nM58 high frame >= 4GiB: PASS\n");
    serial_write_string("M58 high frame write/read: PASS\n");
    serial_write_string("M58 neighboring frame isolation: PASS\n");

    for (index = 0U; index < ((size_t)PMM_PAGE_SIZE / sizeof(uint64_t));
         ++index) {
        high[index] = 0ULL;
        neighbor[index] = 0ULL;
    }
    m58_memory_barrier();
    if (!pmm_free_frame(neighbor_physical) ||
        !pmm_free_frame(high_physical)) {
        m58_fail("high-frame free");
    }
    if (!pmm_get_stats(&after) ||
        (after.free_frames != before.free_frames) ||
        (after.usable_frames != before.usable_frames) ||
        (after.usable_bytes != before.usable_bytes) ||
        (after.region_count != before.region_count) ||
        (after.memory_map_capped != before.memory_map_capped)) {
        m58_fail("cleanup accounting");
    }

    serial_write_string("M58 high frame free: PASS\n");
    serial_write_string("M58 accounting: PASS\n");
    serial_write_string("M58 cleanup: PASS\n");
    serial_write_string("M58 HIGH MEMORY TEST PASSED\n");
    x86_64_halt_forever();
}
