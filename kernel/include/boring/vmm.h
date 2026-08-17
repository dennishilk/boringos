#ifndef BORING_VMM_H
#define BORING_VMM_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/boot_protocol.h>

#define VMM_PAGE_SIZE 4096ULL
#define VMM_FLAG_WRITABLE (1ULL << 1)

struct vmm_stats {
    uint64_t active_root_physical;
    uint64_t hhdm_offset;
    uint64_t owned_page_table_frames;
    uint64_t paging_levels;
};

bool vmm_init(const struct boring_limine_hhdm_response *hhdm,
              const struct boring_limine_paging_mode_response *paging_mode,
              const struct boring_limine_memmap_response *memory_map);
bool vmm_map_page(uintptr_t virtual_address, uint64_t physical_address,
                  uint64_t flags);
bool vmm_unmap_page(uintptr_t virtual_address);
bool vmm_translate(uintptr_t virtual_address, uint64_t *physical_address);
bool vmm_get_stats(struct vmm_stats *stats);
uintptr_t vmm_test_virtual_address(void);

#endif
