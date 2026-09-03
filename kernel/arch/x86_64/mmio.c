#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define VMM_MMIO_WINDOW_BASE ((uintptr_t)0xffffff0002000000ULL)
#define VMM_MMIO_WINDOW_SIZE ((size_t)0x01000000U)
#define VMM_MMIO_WINDOW_PAGES (VMM_MMIO_WINDOW_SIZE / (size_t)VMM_PAGE_SIZE)
#define VMM_PTE_PRESENT (1ULL << 0)
#define VMM_PTE_WRITABLE VMM_FLAG_WRITABLE
#define VMM_PTE_CACHE_DISABLE VMM_FLAG_CACHE_DISABLE
#define VMM_PTE_LARGE (1ULL << 7)
#define VMM_PTE_ADDRESS_MASK 0x000ffffffffff000ULL

static size_t vmm_mmio_next_page;
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#define VMM_FRAMEBUFFER_WINDOW_PAGES \
    (VMM_FRAMEBUFFER_WINDOW_SIZE / (size_t)VMM_PAGE_SIZE)
static bool vmm_framebuffer_window_mapped;

_Static_assert(VMM_MMIO_WINDOW_BASE + VMM_MMIO_WINDOW_SIZE <=
               VMM_FRAMEBUFFER_WINDOW_BASE,
               "PCI MMIO and framebuffer windows must not overlap");
#endif

struct mmio_mapping_span {
    uint64_t physical_page;
    size_t page_offset;
    size_t pages;
};

static bool mmio_is_canonical_4level(uint64_t address) {
    const uint64_t upper = address >> 48U;
    const bool high_half = ((address >> 47U) & 1ULL) != 0ULL;

    return high_half ? (upper == 0xffffULL) : (upper == 0ULL);
}

static bool mmio_hhdm_pointer(uint64_t physical_address,
                              uintptr_t *virtual_address) {
    struct vmm_stats stats;
    uint64_t mapped;

    if ((virtual_address == NULL) || !vmm_get_stats(&stats) ||
        (physical_address > (UINT64_MAX - stats.hhdm_offset))) {
        return false;
    }

    mapped = stats.hhdm_offset + physical_address;
    if (!mmio_is_canonical_4level(mapped)) {
        return false;
    }

    *virtual_address = (uintptr_t)mapped;
    return true;
}

bool vmm_pmm_frame_to_hhdm(uint64_t physical_address,
                           void **virtual_address) {
    uintptr_t mapped;

    if ((virtual_address == NULL) ||
        ((physical_address & (VMM_PAGE_SIZE - 1ULL)) != 0ULL) ||
        !pmm_frame_is_usable(physical_address) ||
        !mmio_hhdm_pointer(physical_address, &mapped)) {
        return false;
    }

    *virtual_address = (void *)mapped;
    return true;
}

static bool mmio_table_from_physical(uint64_t physical_address,
                                     uint64_t **table) {
    uintptr_t mapped;

    if ((table == NULL) ||
        ((physical_address & (VMM_PAGE_SIZE - 1ULL)) != 0ULL) ||
        ((physical_address & ~VMM_PTE_ADDRESS_MASK) != 0ULL) ||
        !mmio_hhdm_pointer(physical_address, &mapped)) {
        return false;
    }

    *table = (uint64_t *)mapped;
    return true;
}

static uint64_t mmio_index(uintptr_t virtual_address, unsigned int shift) {
    return (((uint64_t)virtual_address >> shift) & 0x1ffULL);
}

static bool mmio_replace_leaf(uintptr_t virtual_address,
                              uint64_t expected_ram_frame,
                              uint64_t mmio_frame) {
    struct vmm_stats stats;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t entry;
    uint64_t *leaf;

    if (!vmm_get_stats(&stats) ||
        !mmio_table_from_physical(stats.active_root_physical, &pml4)) {
        return false;
    }

    entry = pml4[mmio_index(virtual_address, 39U)];
    if ((entry & VMM_PTE_PRESENT) == 0ULL) {
        return false;
    }
    if (!mmio_table_from_physical(entry & VMM_PTE_ADDRESS_MASK, &pdpt)) {
        return false;
    }

    entry = pdpt[mmio_index(virtual_address, 30U)];
    if (((entry & VMM_PTE_PRESENT) == 0ULL) ||
        ((entry & VMM_PTE_LARGE) != 0ULL) ||
        !mmio_table_from_physical(entry & VMM_PTE_ADDRESS_MASK, &pd)) {
        return false;
    }

    entry = pd[mmio_index(virtual_address, 21U)];
    if (((entry & VMM_PTE_PRESENT) == 0ULL) ||
        ((entry & VMM_PTE_LARGE) != 0ULL) ||
        !mmio_table_from_physical(entry & VMM_PTE_ADDRESS_MASK, &pt)) {
        return false;
    }

    leaf = &pt[mmio_index(virtual_address, 12U)];
    if (((*leaf & VMM_PTE_PRESENT) == 0ULL) ||
        ((*leaf & VMM_PTE_ADDRESS_MASK) != expected_ram_frame)) {
        return false;
    }

    *leaf = mmio_frame | VMM_PTE_PRESENT | VMM_PTE_WRITABLE |
            VMM_PTE_CACHE_DISABLE;
    x86_64_invalidate_page(virtual_address);
    return true;
}

static bool mmio_map_one(uintptr_t virtual_address, uint64_t mmio_frame) {
    uint64_t staging_frame;

    if (((mmio_frame & (VMM_PAGE_SIZE - 1ULL)) != 0ULL) ||
        ((mmio_frame & ~VMM_PTE_ADDRESS_MASK) != 0ULL) ||
        !pmm_alloc_frame(&staging_frame)) {
        return false;
    }

    if (!vmm_map_page(virtual_address, staging_frame, VMM_FLAG_WRITABLE)) {
        (void)pmm_free_frame(staging_frame);
        return false;
    }

    if (!mmio_replace_leaf(virtual_address, staging_frame, mmio_frame)) {
        (void)vmm_unmap_page(virtual_address);
        (void)pmm_free_frame(staging_frame);
        return false;
    }

    if (!pmm_free_frame(staging_frame)) {
        (void)vmm_unmap_page(virtual_address);
        return false;
    }

    return true;
}

static void mmio_rollback(uintptr_t first_virtual, size_t mapped_pages) {
    size_t index;

    for (index = 0U; index < mapped_pages; ++index) {
        const uintptr_t address = first_virtual +
            (uintptr_t)(index * (size_t)VMM_PAGE_SIZE);
        (void)vmm_unmap_page(address);
    }
}

static bool mmio_calculate_span(uint64_t physical_address,
                                size_t length,
                                struct mmio_mapping_span *mapping) {
    const uint64_t page_mask = VMM_PAGE_SIZE - 1ULL;
    const uint64_t physical_page = physical_address & ~page_mask;
    const size_t page_offset = (size_t)(physical_address & page_mask);
    size_t span;
    size_t pages;
    uint64_t last_page;

    if ((mapping == NULL) || (length == 0U) ||
        (page_offset > (SIZE_MAX - length))) {
        return false;
    }
    span = page_offset + length;
    if (span > (SIZE_MAX - ((size_t)VMM_PAGE_SIZE - 1U))) {
        return false;
    }
    pages = (span + ((size_t)VMM_PAGE_SIZE - 1U)) /
            (size_t)VMM_PAGE_SIZE;
    if ((pages == 0U) ||
        (physical_page > (UINT64_MAX -
         ((uint64_t)(pages - 1U) * VMM_PAGE_SIZE)))) {
        return false;
    }
    last_page = physical_page + ((uint64_t)(pages - 1U) * VMM_PAGE_SIZE);
    if (((physical_page & ~VMM_PTE_ADDRESS_MASK) != 0ULL) ||
        ((last_page & ~VMM_PTE_ADDRESS_MASK) != 0ULL)) {
        return false;
    }

    mapping->physical_page = physical_page;
    mapping->page_offset = page_offset;
    mapping->pages = pages;
    return true;
}

static bool mmio_map_at(uint64_t physical_address,
                        size_t length,
                        uintptr_t first_virtual,
                        size_t capacity_pages,
                        volatile void **virtual_address,
                        size_t *mapped_pages) {
    struct mmio_mapping_span mapping;
    size_t index;
    uintptr_t virtual_end;

    if ((virtual_address == NULL) || (mapped_pages == NULL) ||
        !mmio_calculate_span(physical_address, length, &mapping) ||
        (mapping.pages > capacity_pages) ||
        (mapping.pages > (SIZE_MAX / (size_t)VMM_PAGE_SIZE))) {
        return false;
    }
    if ((uintptr_t)(mapping.pages * (size_t)VMM_PAGE_SIZE) >
        (UINTPTR_MAX - first_virtual)) {
        return false;
    }
    virtual_end = first_virtual +
        (uintptr_t)(mapping.pages * (size_t)VMM_PAGE_SIZE) - 1U;
    if (!mmio_is_canonical_4level((uint64_t)first_virtual) ||
        !mmio_is_canonical_4level((uint64_t)virtual_end)) {
        return false;
    }

    for (index = 0U; index < mapping.pages; ++index) {
        const uintptr_t current_virtual = first_virtual +
            (uintptr_t)(index * (size_t)VMM_PAGE_SIZE);
        const uint64_t current_physical = mapping.physical_page +
            ((uint64_t)index * VMM_PAGE_SIZE);
        struct vmm_mapping_info existing;

        if (!vmm_inspect_mapping(current_virtual, &existing) ||
            !existing.canonical || existing.present ||
            !mmio_map_one(current_virtual, current_physical)) {
            mmio_rollback(first_virtual, index);
            return false;
        }
    }

    *virtual_address = (volatile void *)(first_virtual +
        (uintptr_t)mapping.page_offset);
    *mapped_pages = mapping.pages;
    return true;
}

bool vmm_map_mmio_region(uint64_t physical_address,
                         size_t length,
                         volatile void **virtual_address) {
    size_t pages = 0U;
    uintptr_t first_virtual;

    if ((virtual_address == NULL) ||
        (vmm_mmio_next_page >= VMM_MMIO_WINDOW_PAGES)) {
        return false;
    }
    first_virtual = VMM_MMIO_WINDOW_BASE +
        (uintptr_t)(vmm_mmio_next_page * (size_t)VMM_PAGE_SIZE);
    if ((first_virtual < VMM_MMIO_WINDOW_BASE) ||
        !mmio_map_at(physical_address, length, first_virtual,
                     VMM_MMIO_WINDOW_PAGES - vmm_mmio_next_page,
                     virtual_address, &pages)) {
        return false;
    }

    vmm_mmio_next_page += pages;
    return true;
}

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
bool vmm_framebuffer_mapping_page_count(uint64_t physical_address,
                                        size_t length,
                                        size_t *mapped_pages) {
    struct mmio_mapping_span mapping;

    if ((mapped_pages == NULL) ||
        !mmio_calculate_span(physical_address, length, &mapping)) {
        return false;
    }
    *mapped_pages = mapping.pages;
    return true;
}

bool vmm_map_framebuffer_region(uint64_t physical_address,
                                size_t length,
                                volatile void **virtual_address,
                                size_t *mapped_pages) {
    size_t pages = 0U;

    if (vmm_framebuffer_window_mapped ||
        !mmio_map_at(physical_address, length,
                     VMM_FRAMEBUFFER_WINDOW_BASE,
                     VMM_FRAMEBUFFER_WINDOW_PAGES,
                     virtual_address, &pages)) {
        return false;
    }
    vmm_framebuffer_window_mapped = true;
    *mapped_pages = pages;
    return true;
}
#endif

bool vmm_unmap_mmio_region(volatile void *virtual_address, size_t length) {
    const uintptr_t address = (uintptr_t)virtual_address;
    const uintptr_t page_mask = (uintptr_t)VMM_PAGE_SIZE - 1U;
    const uintptr_t first_page = address & ~page_mask;
    const size_t page_offset = (size_t)(address & page_mask);
    size_t span;
    size_t pages;
    size_t index;
    bool ok = true;

    if ((virtual_address == NULL) || (length == 0U) ||
        (address < VMM_MMIO_WINDOW_BASE) ||
        (page_offset > (SIZE_MAX - length))) {
        return false;
    }

    span = page_offset + length;
    if (span > (SIZE_MAX - ((size_t)VMM_PAGE_SIZE - 1U))) {
        return false;
    }
    pages = (span + ((size_t)VMM_PAGE_SIZE - 1U)) /
            (size_t)VMM_PAGE_SIZE;

    if ((pages == 0U) ||
        (first_page < VMM_MMIO_WINDOW_BASE) ||
        (first_page >= (VMM_MMIO_WINDOW_BASE + VMM_MMIO_WINDOW_SIZE)) ||
        (pages > VMM_MMIO_WINDOW_PAGES) ||
        ((uintptr_t)(pages * (size_t)VMM_PAGE_SIZE) >
         ((VMM_MMIO_WINDOW_BASE + VMM_MMIO_WINDOW_SIZE) - first_page))) {
        return false;
    }

    for (index = 0U; index < pages; ++index) {
        if (!vmm_unmap_page(first_page +
                            (uintptr_t)(index * (size_t)VMM_PAGE_SIZE))) {
            ok = false;
        }
    }

    /* Reclaim only the top span so lower live mappings cannot be overlapped. */
    if (ok && (pages <= vmm_mmio_next_page) &&
        (first_page == VMM_MMIO_WINDOW_BASE +
         (uintptr_t)((vmm_mmio_next_page - pages) *
                     (size_t)VMM_PAGE_SIZE))) {
        vmm_mmio_next_page -= pages;
    }

    return ok;
}
