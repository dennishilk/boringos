#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/pmm.h>

#define MANAGED_FRAME_CAP 1048576ULL

static struct boring_limine_memmap_entry usable(uint64_t base,
                                                 uint64_t length) {
    struct boring_limine_memmap_entry entry;

    entry.base = base;
    entry.length = length;
    entry.type = BORING_LIMINE_MEMMAP_USABLE;
    return entry;
}

static void test_large_map_is_safely_capped(void) {
    struct boring_limine_memmap_entry entry =
        usable(0x100000ULL, (MANAGED_FRAME_CAP + 256ULL) * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&entry};
    struct boring_limine_memmap_response map = {0ULL, 1ULL, entries};
    struct pmm_stats stats;
    uint64_t frame;

    assert(pmm_init(&map));
    assert(pmm_get_stats(&stats));
    assert(stats.usable_frames == MANAGED_FRAME_CAP);
    assert(stats.usable_bytes == MANAGED_FRAME_CAP * PMM_PAGE_SIZE);
    assert(stats.free_frames == MANAGED_FRAME_CAP);
    assert(stats.region_count == 1ULL);
    assert(stats.memory_map_capped);
    assert(pmm_alloc_frame(&frame));
    assert(frame == entry.base);
    assert(pmm_free_frame(frame));
}

static void test_small_map_is_not_capped(void) {
    struct boring_limine_memmap_entry first =
        usable(0x1000ULL, 3ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry second =
        usable(0x10000ULL, 2ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&first, &second};
    struct boring_limine_memmap_response map = {0ULL, 2ULL, entries};
    struct pmm_stats stats;

    assert(pmm_init(&map));
    assert(pmm_get_stats(&stats));
    assert(stats.usable_frames == 5ULL);
    assert(stats.region_count == 2ULL);
    assert(!stats.memory_map_capped);
}

static void test_malformed_maps_still_fail_closed(void) {
    struct boring_limine_memmap_entry first =
        usable(0x1000ULL, 3ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry overlap =
        usable(0x2000ULL, 2ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry overflow =
        usable(UINT64_MAX - PMM_PAGE_SIZE + 1ULL, PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *overlap_entries[] = {&first, &overlap};
    struct boring_limine_memmap_entry *overflow_entries[] = {&overflow};
    struct boring_limine_memmap_response overlap_map =
        {0ULL, 2ULL, overlap_entries};
    struct boring_limine_memmap_response overflow_map =
        {0ULL, 1ULL, overflow_entries};

    assert(!pmm_init(&overlap_map));
    assert(!pmm_init(&overflow_map));
}

int main(void) {
    test_large_map_is_safely_capped();
    test_small_map_is_not_capped();
    test_malformed_maps_still_fail_closed();
    (void)puts("PMM real-hardware readiness host tests passed");
    return 0;
}
