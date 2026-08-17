#include <stddef.h>
#include <stdint.h>

#include <boring/pmm.h>

#define PMM_MAX_MEMORY_MAP_ENTRIES 256ULL
#define PMM_MAX_REGIONS 64ULL
#define PMM_MAX_FRAMES 1048576ULL
#define PMM_BITMAP_WORDS 16384ULL
#define PMM_BITS_PER_WORD 64ULL

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

static void pmm_reset_state(void) {
    uint64_t index;

    pmm_region_count = 0ULL;
    pmm_total_frames = 0ULL;
    pmm_free_frames = 0ULL;
    pmm_initialized = false;

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

static bool pmm_bit_is_set(uint64_t frame_index) {
    const uint64_t word_index = frame_index / PMM_BITS_PER_WORD;
    const uint64_t bit_index = frame_index % PMM_BITS_PER_WORD;
    const uint64_t mask = 1ULL << (unsigned int)bit_index;

    return (pmm_bitmap[word_index] & mask) != 0ULL;
}

static void pmm_set_bit(uint64_t frame_index) {
    const uint64_t word_index = frame_index / PMM_BITS_PER_WORD;
    const uint64_t bit_index = frame_index % PMM_BITS_PER_WORD;
    const uint64_t mask = 1ULL << (unsigned int)bit_index;

    pmm_bitmap[word_index] |= mask;
}

static void pmm_clear_bit(uint64_t frame_index) {
    const uint64_t word_index = frame_index / PMM_BITS_PER_WORD;
    const uint64_t bit_index = frame_index % PMM_BITS_PER_WORD;
    const uint64_t mask = 1ULL << (unsigned int)bit_index;

    pmm_bitmap[word_index] &= ~mask;
}

static bool pmm_find_frame(uint64_t physical_address, uint64_t *frame_index) {
    uint64_t region_index;

    if ((physical_address % PMM_PAGE_SIZE) != 0ULL) {
        return false;
    }

    for (region_index = 0ULL; region_index < pmm_region_count; ++region_index) {
        const struct pmm_region *region = &pmm_regions[region_index];
        const uint64_t region_bytes = region->frame_count * PMM_PAGE_SIZE;
        const uint64_t region_end = region->base + region_bytes;

        if ((physical_address >= region->base) &&
            (physical_address < region_end)) {
            const uint64_t offset = physical_address - region->base;
            const uint64_t local_frame = offset / PMM_PAGE_SIZE;

            if (frame_index != NULL) {
                *frame_index = region->bitmap_base + local_frame;
            }
            return true;
        }
    }

    return false;
}

bool pmm_init(const struct boring_limine_memmap_response *memory_map) {
    uint64_t entry_index;

    pmm_reset_state();

    if ((memory_map == NULL) || (memory_map->entries == NULL) ||
        (memory_map->entry_count == 0ULL) ||
        (memory_map->entry_count > PMM_MAX_MEMORY_MAP_ENTRIES)) {
        return false;
    }

    for (entry_index = 0ULL; entry_index < memory_map->entry_count;
         ++entry_index) {
        uint64_t first_end;
        uint64_t other_index;
        const struct boring_limine_memmap_entry *entry =
            memory_map->entries[entry_index];

        if (!pmm_entry_end(entry, &first_end)) {
            return false;
        }

        for (other_index = entry_index + 1ULL;
             other_index < memory_map->entry_count; ++other_index) {
            uint64_t second_end;
            const struct boring_limine_memmap_entry *other =
                memory_map->entries[other_index];

            if (!pmm_entry_end(other, &second_end)) {
                return false;
            }

            if (pmm_ranges_overlap(entry->base, first_end,
                                   other->base, second_end)) {
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
        const struct boring_limine_memmap_entry *entry =
            memory_map->entries[entry_index];

        if (entry->type != BORING_LIMINE_MEMMAP_USABLE) {
            continue;
        }

        if (!pmm_entry_end(entry, &raw_end) ||
            !pmm_align_up(entry->base, &aligned_base)) {
            pmm_reset_state();
            return false;
        }

        aligned_end = pmm_align_down(raw_end);
        if (aligned_end <= aligned_base) {
            continue;
        }

        frame_count = (aligned_end - aligned_base) / PMM_PAGE_SIZE;
        if ((frame_count == 0ULL) ||
            (frame_count > (PMM_MAX_FRAMES - pmm_total_frames)) ||
            (pmm_region_count >= PMM_MAX_REGIONS)) {
            pmm_reset_state();
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
        return false;
    }

    pmm_free_frames = pmm_total_frames;
    pmm_initialized = true;
    return true;
}

bool pmm_alloc_frame(uint64_t *physical_address) {
    uint64_t region_index;

    if ((!pmm_initialized) || (physical_address == NULL) ||
        (pmm_free_frames == 0ULL)) {
        return false;
    }

    for (region_index = 0ULL; region_index < pmm_region_count; ++region_index) {
        uint64_t local_frame;
        const struct pmm_region *region = &pmm_regions[region_index];

        for (local_frame = 0ULL; local_frame < region->frame_count;
             ++local_frame) {
            const uint64_t frame_index = region->bitmap_base + local_frame;

            if (!pmm_bit_is_set(frame_index)) {
                pmm_set_bit(frame_index);
                --pmm_free_frames;
                *physical_address =
                    region->base + (local_frame * PMM_PAGE_SIZE);
                return true;
            }
        }
    }

    return false;
}

bool pmm_free_frame(uint64_t physical_address) {
    uint64_t frame_index;

    if ((!pmm_initialized) ||
        !pmm_find_frame(physical_address, &frame_index) ||
        !pmm_bit_is_set(frame_index)) {
        return false;
    }

    pmm_clear_bit(frame_index);
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
    if ((!pmm_initialized) || (stats == NULL)) {
        return false;
    }

    stats->usable_frames = pmm_total_frames;
    stats->usable_bytes = pmm_total_frames * PMM_PAGE_SIZE;
    stats->free_frames = pmm_free_frames;
    stats->region_count = pmm_region_count;
    return true;
}
