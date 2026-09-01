#ifndef BORING_VMM_H
#define BORING_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>

#define VMM_PAGE_SIZE 4096ULL
#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_CACHE_DISABLE (1ULL << 4)

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

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
uint8_t boring_m61_vmm_failure_reason(void);
#endif

/*
 * Explicit PCI-MMIO path. Unlike vmm_map_page(), the physical pages mapped by
 * this interface are not PMM-owned RAM. The mapper uses a bounded dedicated
 * kernel virtual window and applies x86 PCD at the leaf PTE only.
 */
bool vmm_map_mmio_region(uint64_t physical_address,
                         size_t length,
                         volatile void **virtual_address);
bool vmm_unmap_mmio_region(volatile void *virtual_address, size_t length);

/* Convert one PMM-owned frame to its existing HHDM virtual address for DMA. */
bool vmm_pmm_frame_to_hhdm(uint64_t physical_address,
                           void **virtual_address);

#endif
