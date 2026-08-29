#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/pmm.h>

#define MANAGED_FRAME_CAP 8388608ULL
#define FOUR_GIB 0x100000000ULL
#define THIRTY_TWO_GIB 0x800000000ULL

static struct boring_limine_memmap_entry usable(uint64_t base,
                                                 uint64_t length) {
    struct boring_limine_memmap_entry entry;

    entry.base = base;
    entry.length = length;
    entry.type = BORING_LIMINE_MEMMAP_USABLE;
    return entry;
}

static void test_configured_capacity_and_last_bitmap_frame(void) {
    struct boring_limine_memmap_entry entry =
        usable(0x100000ULL, (MANAGED_FRAME_CAP + 256ULL) * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&entry};
    struct boring_limine_memmap_response map = {0ULL, 1ULL, entries};
    struct pmm_stats stats;
    const uint64_t last =
        entry.base + ((MANAGED_FRAME_CAP - 1ULL) * PMM_PAGE_SIZE);
    const uint64_t one_past = entry.base + (MANAGED_FRAME_CAP * PMM_PAGE_SIZE);
    uint64_t frame = 0ULL;

    assert(pmm_init(&map));
    assert(pmm_get_stats(&stats));
    assert(stats.usable_frames == MANAGED_FRAME_CAP);
    assert(stats.usable_bytes == MANAGED_FRAME_CAP * PMM_PAGE_SIZE);
    assert(stats.free_frames == MANAGED_FRAME_CAP);
    assert(stats.region_count == 1ULL);
    assert(stats.memory_map_capped);
    assert(pmm_frame_is_usable(last));
    assert(!pmm_frame_is_usable(one_past));
    assert(pmm_alloc_frame_in_range(last, one_past, &frame));
    assert(frame == last);
    assert(!pmm_alloc_frame_in_range(one_past, one_past + PMM_PAGE_SIZE,
                                     &frame));
    assert(pmm_free_frame(last));
}

static void test_exact_32gib_capacity_is_not_capped(void) {
    struct boring_limine_memmap_entry entry =
        usable(0x100000ULL, MANAGED_FRAME_CAP * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&entry};
    struct boring_limine_memmap_response map = {0ULL, 1ULL, entries};
    struct pmm_stats stats;

    assert(pmm_init(&map));
    assert(pmm_get_stats(&stats));
    assert(stats.usable_frames == MANAGED_FRAME_CAP);
    assert(stats.usable_bytes == THIRTY_TWO_GIB);
    assert(!stats.memory_map_capped);
}

static void test_four_gib_allocation_boundaries(void) {
    struct boring_limine_memmap_entry entry =
        usable(FOUR_GIB - (2ULL * PMM_PAGE_SIZE), 4ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&entry};
    struct boring_limine_memmap_response map = {0ULL, 1ULL, entries};
    struct pmm_stats before;
    struct pmm_stats after;
    uint64_t below = 0ULL;
    uint64_t at = 0ULL;
    uint64_t above = 0ULL;

    assert(pmm_init(&map));
    assert(pmm_get_stats(&before));
    assert(pmm_alloc_frame_in_range(FOUR_GIB - PMM_PAGE_SIZE, FOUR_GIB,
                                    &below));
    assert(below == FOUR_GIB - PMM_PAGE_SIZE);
    assert(pmm_alloc_frame_in_range(FOUR_GIB, FOUR_GIB + PMM_PAGE_SIZE, &at));
    assert(at == FOUR_GIB);
    assert(pmm_alloc_frame_in_range(FOUR_GIB + PMM_PAGE_SIZE,
                                    FOUR_GIB + (2ULL * PMM_PAGE_SIZE),
                                    &above));
    assert(above == FOUR_GIB + PMM_PAGE_SIZE);
    assert(pmm_get_stats(&after));
    assert(after.free_frames + 3ULL == before.free_frames);
    assert(!pmm_free_frame(at + 1ULL));
    assert(pmm_free_frame(below));
    assert(pmm_free_frame(at));
    assert(!pmm_free_frame(at));
    assert(pmm_free_frame(above));
    assert(pmm_get_stats(&after));
    assert(after.free_frames == before.free_frames);
}

static void test_near_32gib_physical_boundary(void) {
    struct boring_limine_memmap_entry entry =
        usable(THIRTY_TWO_GIB - (2ULL * PMM_PAGE_SIZE),
               4ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&entry};
    struct boring_limine_memmap_response map = {0ULL, 1ULL, entries};
    uint64_t frame = 0ULL;

    assert(pmm_init(&map));
    assert(pmm_frame_is_usable(THIRTY_TWO_GIB - PMM_PAGE_SIZE));
    assert(pmm_frame_is_usable(THIRTY_TWO_GIB));
    assert(pmm_frame_is_usable(THIRTY_TWO_GIB + PMM_PAGE_SIZE));
    assert(pmm_alloc_frame_in_range(THIRTY_TWO_GIB,
                                    THIRTY_TWO_GIB + PMM_PAGE_SIZE, &frame));
    assert(frame == THIRTY_TWO_GIB);
    assert(pmm_free_frame(frame));
}

static void test_region_accumulation_and_range_selection(void) {
    struct boring_limine_memmap_entry first =
        usable(0x1000ULL, 3ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry second =
        usable(FOUR_GIB + 0x2000ULL, 2ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&first, &second};
    struct boring_limine_memmap_response map = {0ULL, 2ULL, entries};
    struct pmm_stats stats;
    uint64_t low = 0ULL;
    uint64_t high = 0ULL;

    assert(pmm_init(&map));
    assert(pmm_get_stats(&stats));
    assert(stats.usable_frames == 5ULL);
    assert(stats.region_count == 2ULL);
    assert(!stats.memory_map_capped);
    assert(pmm_alloc_frame(&low));
    assert(low == first.base);
    assert(pmm_alloc_frame_in_range(FOUR_GIB, UINT64_MAX, &high));
    assert(high == second.base);
    assert(pmm_free_frame(low));
    assert(pmm_free_frame(high));
}

static void test_invalid_ranges_and_addresses_are_rejected(void) {
    struct boring_limine_memmap_entry entry =
        usable(0x2000ULL, 2ULL * PMM_PAGE_SIZE);
    struct boring_limine_memmap_entry *entries[] = {&entry};
    struct boring_limine_memmap_response map = {0ULL, 1ULL, entries};
    uint64_t frame = 0ULL;

    assert(pmm_init(&map));
    assert(!pmm_alloc_frame_in_range(0x3000ULL, 0x3000ULL, &frame));
    assert(!pmm_alloc_frame_in_range(UINT64_MAX - 1ULL, UINT64_MAX, &frame));
    assert(!pmm_alloc_frame_in_range(0ULL, UINT64_MAX, NULL));
    assert(!pmm_frame_is_usable(0x1000ULL));
    assert(!pmm_frame_is_usable(0x2001ULL));
    assert(!pmm_free_frame(0x1000ULL));
    assert(!pmm_free_frame(0x2001ULL));
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
    test_configured_capacity_and_last_bitmap_frame();
    test_exact_32gib_capacity_is_not_capped();
    test_four_gib_allocation_boundaries();
    test_near_32gib_physical_boundary();
    test_region_accumulation_and_range_selection();
    test_invalid_ranges_and_addresses_are_rejected();
    test_malformed_maps_still_fail_closed();
    (void)puts("M58 high-memory PMM host tests passed");
    return 0;
}
