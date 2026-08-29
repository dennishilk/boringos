#ifndef BORING_PMM_H
#define BORING_PMM_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/boot_protocol.h>

#define PMM_PAGE_SIZE 4096ULL

struct pmm_stats {
    uint64_t usable_bytes;
    uint64_t usable_frames;
    uint64_t free_frames;
    uint64_t region_count;
    bool memory_map_capped;
};

bool pmm_init(const struct boring_limine_memmap_response *memory_map);
bool pmm_alloc_frame(uint64_t *physical_address);
bool pmm_alloc_frame_in_range(uint64_t minimum_physical_address,
                              uint64_t maximum_physical_address_exclusive,
                              uint64_t *physical_address);
bool pmm_free_frame(uint64_t physical_address);
bool pmm_frame_is_usable(uint64_t physical_address);
bool pmm_get_stats(struct pmm_stats *stats);

#endif
