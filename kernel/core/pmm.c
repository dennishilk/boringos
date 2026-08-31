#include <stddef.h>
#include <stdint.h>

#include <boring/pmm.h>

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/io.h>
#define PMM_M61_FAILURE_POST(code) \
    x86_64_out8((uint16_t)0x80U, (uint8_t)(code))
#else
#define PMM_M61_FAILURE_POST(code) ((void)0)
#endif

#define PMM_MAX_MEMORY_MAP_ENTRIES 256ULL
#define PMM_MAX_REGIONS 64ULL
#define PMM_MAX_FRAMES 8388608ULL
#define PMM_BITS_PER_WORD 64ULL
#define PMM_BITMAP_WORDS \
    ((PMM_MAX_FRAMES + PMM_BITS_PER_WORD - 1ULL) / PMM_BITS_PER_WORD)

_Static_assert(PMM_MAX_FRAMES <= (UINT64_MAX / PMM_PAGE_SIZE),
               "PMM frame capacity must fit byte accounting");
_Static_assert((PMM_BITMAP_WORDS * PMM_BITS_PER_WORD) >= PMM_MAX_FRAMES,
               "PMM bitmap must cover configured frame capacity");

struct pmm_region {
    uint64_t base;
    uint64_t frame_count;
    uint64_t bitmap_base;
};

static struct pmm_region pmm_regions[PMM_MAX_REGIONS];
static uint64_t pmm_bitmap[PMM_BITMAP_WORDS];
static uint64_t pmm_region_count;
static uint64_t pmm_total_frames;
static uint64_t pmm_free_frames;
static bool pmm_initialized;
static bool pmm_memory_map_capped;

static void pmm_reset_state(void) {
    uint64_t index;

    pmm_region_count = 0ULL;
    pmm_total_frames = 0ULL;
    pmm_free_frames = 0ULL;
    pmm_initialized = false;
    pmm_memory_map_capped = false;

    for (index = 0ULL; index < PMM_MAX_REGIONS; ++index) {
        pmm_regions[index].base = 0ULL;
        pmm_regions[index].frame_count = 0ULL;
        pmm_regions[index].bitmap_base = 0ULL;
    }

    for (index = 0ULL; index < PMM_BITMAP_WORDS; ++index) {
        pmm_bitmap[index] = 0ULL;
    }
}

static bool pmm_entry_end(const struct boring_limine_memmap_entry *entry,
                          uint64_t *end) {
    if ((entry == NULL) || (end == NULL) || (entry->length == 0ULL)) {
        return false;
    }

    if (entry->base > (UINT64_MAX - entry->length)) {
        return false;
    }

    *end = entry->base + entry->length;
    return true;
}

static bool pmm_ranges_overlap(uint64_t first_base, uint64_t first_end,
                               uint64_t second_base, uint64_t second_end) {
    return (first_base < second_end) && (second_base < first_end);
}

static bool pmm_overlap_requires_rejection(uint64_t first_type,
                                           uint64_t second_type) {
    /* Limine guarantees global non-overlap only for these two entry types. */
    return (first_type == BORING_LIMINE_MEMMAP_USABLE) ||
           (second_type == BORING_LIMINE_MEMMAP_USABLE) ||
           (first_type == BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) ||
           (second_type == BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE);
}

static bool pmm_align_up(uint64_t value, uint64_t *aligned) {
    const uint64_t mask = PMM_PAGE_SIZE - 1ULL;

    if (aligned == NULL) {
        return false;
    }

    if (value > (UINT64_MAX - mask)) {
        return false;
    }

    *aligned = (value + mask) & ~mask;
    return true;
}

static uint64_t pmm_align_down(uint64_t value) {
    return value & ~(PMM_PAGE_SIZE - 1ULL);
}

static bool pmm_bitmap_index_valid(uint64_t frame_index) {
    return (frame_index < pmm_total_frames) &&
           (frame_index < PMM_MAX_FRAMES) &&
           ((frame_index / PMM_BITS_PER_WORD) < PMM_BITMAP_WORDS);
}

static bool pmm_bit_is_set(uint64_t frame_index) {
    uint64_t word_index;
    uint64_t bit_index;
    uint64_t mask;

    if (!pmm_bitmap_index_valid(frame_index)) {
        return false;
    }

    word_index = frame_index / PMM_BITS_PER_WORD;
    bit_index = frame_index % PMM_BITS_PER_WORD;
    mask = 1ULL << (unsigned int)bit_index;
    return (pmm_bitmap[word_index] & mask) != 0ULL;
}

static bool pmm_set_bit(uint64_t frame_index) {
    uint64_t word_index;
    uint64_t bit_index;
    uint64_t mask;

    if (!pmm_bitmap_index_valid(frame_index)) {
        return false;
    }

    word_index = frame_index / PMM_BITS_PER_WORD;
    bit_index = frame_index % PMM_BITS_PER_WORD;
    mask = 1ULL << (unsigned int)bit_index;
    pmm_bitmap[word_index] |= mask;
    return true;
}

static bool pmm_clear_bit(uint64_t frame_index) {
    uint64_t word_index;
    uint64_t bit_index;
    uint64_t mask;

    if (!pmm_bitmap_index_valid(frame_index)) {
        return false;
    }

    word_index = frame_index / PMM_BITS_PER_WORD;
    bit_index = frame_index % PMM_BITS_PER_WORD;
    mask = 1ULL << (unsigned int)bit_index;
    pmm_bitmap[word_index] &= ~mask;
    return true;
}

static bool pmm_region_end(const struct pmm_region *region, uint64_t *end) {
    uint64_t region_bytes;

    if ((region == NULL) || (end == NULL) ||
        (region->frame_count > (UINT64_MAX / PMM_PAGE_SIZE))) {
        return false;
    }

    region_bytes = region->frame_count * PMM_PAGE_SIZE;
    if (region->base > (UINT64_MAX - region_bytes)) {
        return false;
    }

    *end = region->base + region_bytes;
    return true;
}

static bool pmm_find_frame(uint64_t physical_address, uint64_t *frame_index) {
    uint64_t region_index;

    if ((physical_address % PMM_PAGE_SIZE) != 0ULL) {
        return false;
    }

    for (region_index = 0ULL; region_index < pmm_region_count; ++region_index) {
        const struct pmm_region *region = &pmm_regions[region_index];
        uint64_t region_end;

        if (!pmm_region_end(region, &region_end)) {
            return false;
        }

        if ((physical_address >= region->base) &&
            (physical_address < region_end)) {
            const uint64_t offset = physical_address - region->base;
            const uint64_t local_frame = offset / PMM_PAGE_SIZE;
            uint64_t candidate;

            if (region->bitmap_base > (UINT64_MAX - local_frame)) {
                return false;
            }
            candidate = region->bitmap_base + local_frame;
            if (!pmm_bitmap_index_valid(candidate)) {
                return false;
            }
            if (frame_index != NULL) {
                *frame_index = candidate;
            }
            return true;
        }
    }

    return false;
}

bool pmm_init(const struct boring_limine_memmap_response *memory_map) {
    uint64_t entry_index;

    pmm_reset_state();

    if (memory_map == NULL) {
        PMM_M61_FAILURE_POST(0xA0U);
        return false;
    }
    if (memory_map->entries == NULL) {
        PMM_M61_FAILURE_POST(0xA9U);
        return false;
    }
    if (memory_map->entry_count == 0ULL) {
        PMM_M61_FAILURE_POST(0xAAU);
        return false;
    }
    if (memory_map->entry_count > PMM_MAX_MEMORY_MAP_ENTRIES) {
        PMM_M61_FAILURE_POST(0xABU);
        return false;
    }

    for (entry_index = 0ULL; entry_index < memory_map->entry_count;
         ++entry_index) {
        uint64_t first_end;
        uint64_t other_index;
        const struct boring_limine_memmap_entry *entry =
            memory_map->entries[entry_index];

        if (!pmm_entry_end(entry, &first_end)) {
            PMM_M61_FAILURE_POST(0xA1U);
            return false;
        }

        for (other_index = entry_index + 1ULL;
             other_index < memory_map->entry_count; ++other_index) {
            uint64_t second_end;
            const struct boring_limine_memmap_entry *other =
                memory_map->entries[other_index];

            if (!pmm_entry_end(other, &second_end)) {
                PMM_M61_FAILURE_POST(0xA2U);
                return false;
            }

            if (pmm_ranges_overlap(entry->base, first_end,
                                   other->base, second_end) &&
                pmm_overlap_requires_rejection(entry->type, other->type)) {
                PMM_M61_FAILURE_POST(0xA3U);
                return false;
            }
        }
    }

    for (entry_index = 0ULL; entry_index < memory_map->entry_count;
         ++entry_index) {
        uint64_t raw_end;
        uint64_t aligned_base;
        uint64_t aligned_end;
        uint64_t frame_count;
        uint64_t remaining_frames;
        const struct boring_limine_memmap_entry *entry =
            memory_map->entries[entry_index];

        if (entry->type != BORING_LIMINE_MEMMAP_USABLE) {
            continue;
        }

        if (!pmm_entry_end(entry, &raw_end)) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA4U);
            return false;
        }
        if (!pmm_align_up(entry->base, &aligned_base)) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA5U);
            return false;
        }

        aligned_end = pmm_align_down(raw_end);
        if (aligned_end <= aligned_base) {
            continue;
        }

        frame_count = (aligned_end - aligned_base) / PMM_PAGE_SIZE;
        if (frame_count == 0ULL) {
            continue;
        }
        if (pmm_total_frames > PMM_MAX_FRAMES) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA6U);
            return false;
        }
        remaining_frames = PMM_MAX_FRAMES - pmm_total_frames;
        if ((remaining_frames == 0ULL) ||
            (pmm_region_count >= PMM_MAX_REGIONS)) {
            pmm_memory_map_capped = true;
            continue;
        }
        if (frame_count > remaining_frames) {
            frame_count = remaining_frames;
            pmm_memory_map_capped = true;
        }
        if ((pmm_total_frames > (UINT64_MAX - frame_count)) ||
            ((pmm_total_frames + frame_count) > PMM_MAX_FRAMES)) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA7U);
            return false;
        }

        pmm_regions[pmm_region_count].base = aligned_base;
        pmm_regions[pmm_region_count].frame_count = frame_count;
        pmm_regions[pmm_region_count].bitmap_base = pmm_total_frames;
        ++pmm_region_count;
        pmm_total_frames += frame_count;
    }

    if (pmm_total_frames == 0ULL) {
        pmm_reset_state();
        PMM_M61_FAILURE_POST(0xA8U);
        return false;
    }

    pmm_free_frames = pmm_total_frames;
    pmm_initialized = true;
    return true;
}

bool pmm_alloc_frame_in_range(uint64_t minimum_physical_address,
                              uint64_t maximum_physical_address_exclusive,
                              uint64_t *physical_address) {
    uint64_t aligned_minimum;
    uint64_t region_index;

    if ((!pmm_initialized) || (physical_address == NULL) ||
        (pmm_free_frames == 0ULL) ||
        (minimum_physical_address >= maximum_physical_address_exclusive) ||
        !pmm_align_up(minimum_physical_address, &aligned_minimum) ||
        (aligned_minimum >= maximum_physical_address_exclusive)) {
        return false;
    }

    for (region_index = 0ULL; region_index < pmm_region_count; ++region_index) {
        const struct pmm_region *region = &pmm_regions[region_index];
        uint64_t region_end;
        uint64_t search_base;
        uint64_t local_frame;

        if (!pmm_region_end(region, &region_end)) {
            return false;
        }
        search_base = region->base;
        if (search_base < aligned_minimum) {
            search_base = aligned_minimum;
        }
        if ((search_base >= region_end) ||
            (search_base >= maximum_physical_address_exclusive)) {
            continue;
        }

        local_frame = (search_base - region->base) / PMM_PAGE_SIZE;
        for (; local_frame < region->frame_count; ++local_frame) {
            const uint64_t frame_index = region->bitmap_base + local_frame;
            const uint64_t frame_address =
                region->base + (local_frame * PMM_PAGE_SIZE);

            if (frame_address >= maximum_physical_address_exclusive) {
                break;
            }
            if (!pmm_bitmap_index_valid(frame_index)) {
                return false;
            }
            if (!pmm_bit_is_set(frame_index)) {
                if (!pmm_set_bit(frame_index)) {
                    return false;
                }
                --pmm_free_frames;
                *physical_address = frame_address;
                return true;
            }
        }
    }

    return false;
}

bool pmm_alloc_frame(uint64_t *physical_address) {
    return pmm_alloc_frame_in_range(0ULL, UINT64_MAX, physical_address);
}

bool pmm_free_frame(uint64_t physical_address) {
    uint64_t frame_index;

    if ((!pmm_initialized) ||
        !pmm_find_frame(physical_address, &frame_index) ||
        !pmm_bit_is_set(frame_index) || !pmm_clear_bit(frame_index)) {
        return false;
    }

    ++pmm_free_frames;
    return true;
}

bool pmm_frame_is_usable(uint64_t physical_address) {
    if (!pmm_initialized) {
        return false;
    }

    return pmm_find_frame(physical_address, NULL);
}

bool pmm_get_stats(struct pmm_stats *stats) {
    if ((!pmm_initialized) || (stats == NULL) ||
        (pmm_total_frames > (UINT64_MAX / PMM_PAGE_SIZE))) {
        return false;
    }

    stats->usable_frames = pmm_total_frames;
    stats->usable_bytes = pmm_total_frames * PMM_PAGE_SIZE;
    stats->free_frames = pmm_free_frames;
    stats->region_count = pmm_region_count;
    stats->memory_map_capped = pmm_memory_map_capped;
    return true;
}
