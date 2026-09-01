#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define VMM_PAGE_ENTRIES 512ULL
#define VMM_ENTRY_PRESENT (1ULL << 0)
#define VMM_ENTRY_WRITABLE (1ULL << 1)
#define VMM_ENTRY_LARGE_PAGE (1ULL << 7)
#define VMM_ENTRY_ADDRESS_MASK 0x000ffffffffff000ULL
#define VMM_ALLOWED_FLAGS VMM_FLAG_WRITABLE
#define VMM_MAX_MEMORY_MAP_ENTRIES 256ULL
#define VMM_MAX_OWNED_TABLE_FRAMES 64ULL
#define VMM_TEST_REGION_BASE 0xffffff0000000000ULL
#define VMM_TEST_REGION_SIZE 0x0000000000200000ULL
#define VMM_KERNEL_HIGHER_HALF_MIN 0xffffffff80000000ULL

struct vmm_created_table {
    uint64_t *parent_entry;
    uint64_t physical_address;
};

struct vmm_walk_path {
    uint64_t *pml4_entry;
    uint64_t *pdpt_entry;
    uint64_t *pd_entry;
    uint64_t *pt_entry;
    uint64_t pdpt_physical;
    uint64_t pd_physical;
    uint64_t pt_physical;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
};

enum vmm_lookup_result {
    VMM_LOOKUP_ERROR = 0,
    VMM_LOOKUP_UNMAPPED = 1,
    VMM_LOOKUP_MAPPED = 2
};

static uint64_t vmm_hhdm_offset;
static uint64_t vmm_root_physical;
static uint64_t *vmm_root_table;
static const struct boring_limine_memmap_response *vmm_memory_map;
static uint64_t vmm_owned_table_frames[VMM_MAX_OWNED_TABLE_FRAMES];
static uint64_t vmm_owned_table_count;
static bool vmm_hhdm_ready;
static bool vmm_initialized;

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
static uint8_t vmm_m61_failure_reason;

#define VMM_M61_CLEAR_FAILURE() \
    do { vmm_m61_failure_reason = 0U; } while (0)
#define VMM_M61_REJECT(code) \
    do { \
        vmm_m61_failure_reason = (uint8_t)(code); \
        return false; \
    } while (0)

uint8_t boring_m61_vmm_failure_reason(void) {
    return vmm_m61_failure_reason;
}
#else
#define VMM_M61_CLEAR_FAILURE() do { } while (0)
#define VMM_M61_REJECT(code) \
    do { \
        (void)sizeof(code); \
        return false; \
    } while (0)
#endif

static void vmm_reset_state(void) {
    uint64_t index;

    vmm_hhdm_offset = 0ULL;
    vmm_root_physical = 0ULL;
    vmm_root_table = NULL;
    vmm_memory_map = NULL;
    vmm_owned_table_count = 0ULL;
    vmm_hhdm_ready = false;
    vmm_initialized = false;

    for (index = 0ULL; index < VMM_MAX_OWNED_TABLE_FRAMES; ++index) {
        vmm_owned_table_frames[index] = 0ULL;
    }
}

static bool vmm_is_page_aligned(uint64_t value) {
    return (value & (VMM_PAGE_SIZE - 1ULL)) == 0ULL;
}

static bool vmm_is_canonical_4level(uint64_t address) {
    const uint64_t upper = address >> 48U;
    const bool high_half = ((address >> 47U) & 1ULL) != 0ULL;

    return high_half ? (upper == 0xffffULL) : (upper == 0ULL);
}

static bool vmm_physical_supported(uint64_t physical_address) {
    return vmm_is_page_aligned(physical_address) &&
           ((physical_address & ~VMM_ENTRY_ADDRESS_MASK) == 0ULL);
}

static bool vmm_memory_entry_end(
    const struct boring_limine_memmap_entry *entry,
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

static bool vmm_memmap_type_has_hhdm(uint64_t type) {
    return (type == BORING_LIMINE_MEMMAP_USABLE) ||
           (type == BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) ||
           (type == BORING_LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) ||
           (type == BORING_LIMINE_MEMMAP_FRAMEBUFFER);
}

static bool vmm_physical_page_in_hhdm_map(uint64_t physical_address) {
    uint64_t index;
    uint64_t physical_end;

    if ((vmm_memory_map == NULL) ||
        !vmm_physical_supported(physical_address) ||
        (physical_address > (UINT64_MAX - VMM_PAGE_SIZE))) {
        return false;
    }

    physical_end = physical_address + VMM_PAGE_SIZE;

    for (index = 0ULL; index < vmm_memory_map->entry_count; ++index) {
        uint64_t entry_end;
        const struct boring_limine_memmap_entry *entry =
            vmm_memory_map->entries[index];

        if (!vmm_memmap_type_has_hhdm(entry->type) ||
            !vmm_memory_entry_end(entry, &entry_end)) {
            continue;
        }

        if ((physical_address >= entry->base) &&
            (physical_end <= entry_end)) {
            return true;
        }
    }

    return false;
}

static bool vmm_physical_to_hhdm(uint64_t physical_address,
                                 uintptr_t *virtual_address) {
    uint64_t mapped_address;

    if ((!vmm_hhdm_ready) || (virtual_address == NULL) ||
        (physical_address > (UINT64_MAX - vmm_hhdm_offset))) {
        return false;
    }

    mapped_address = vmm_hhdm_offset + physical_address;
    if (!vmm_is_canonical_4level(mapped_address)) {
        return false;
    }

    *virtual_address = (uintptr_t)mapped_address;
    return true;
}

static bool vmm_table_from_physical(uint64_t physical_address,
                                    uint64_t **table) {
    uintptr_t virtual_address;

    if ((table == NULL) ||
        !vmm_physical_page_in_hhdm_map(physical_address) ||
        !vmm_physical_to_hhdm(physical_address, &virtual_address)) {
        return false;
    }

    *table = (uint64_t *)virtual_address;
    return true;
}

static uint64_t vmm_index_for(uintptr_t virtual_address,
                              unsigned int shift) {
    return (((uint64_t)virtual_address >> shift) & 0x1ffULL);
}

static bool vmm_owned_contains(uint64_t physical_address) {
    uint64_t index;

    for (index = 0ULL; index < vmm_owned_table_count; ++index) {
        if (vmm_owned_table_frames[index] == physical_address) {
            return true;
        }
    }

    return false;
}

static bool vmm_owned_add(uint64_t physical_address) {
    if ((vmm_owned_table_count >= VMM_MAX_OWNED_TABLE_FRAMES) ||
        vmm_owned_contains(physical_address)) {
        return false;
    }

    vmm_owned_table_frames[vmm_owned_table_count] = physical_address;
    ++vmm_owned_table_count;
    return true;
}

static bool vmm_owned_remove(uint64_t physical_address) {
    uint64_t index;

    for (index = 0ULL; index < vmm_owned_table_count; ++index) {
        if (vmm_owned_table_frames[index] == physical_address) {
            uint64_t move_index;

            for (move_index = index + 1ULL;
                 move_index < vmm_owned_table_count; ++move_index) {
                vmm_owned_table_frames[move_index - 1ULL] =
                    vmm_owned_table_frames[move_index];
            }

            --vmm_owned_table_count;
            vmm_owned_table_frames[vmm_owned_table_count] = 0ULL;
            return true;
        }
    }

    return false;
}

static bool vmm_allocate_table(uint64_t *physical_address,
                               uint64_t **table) {
    uint64_t frame;
    uint64_t *mapped_table;
    uint64_t index;

    if ((physical_address == NULL) || (table == NULL) ||
        (vmm_owned_table_count >= VMM_MAX_OWNED_TABLE_FRAMES) ||
        !pmm_alloc_frame(&frame)) {
        return false;
    }

    if (!vmm_physical_supported(frame) || !pmm_frame_is_usable(frame) ||
        !vmm_table_from_physical(frame, &mapped_table)) {
        (void)pmm_free_frame(frame);
        return false;
    }

    for (index = 0ULL; index < VMM_PAGE_ENTRIES; ++index) {
        mapped_table[index] = 0ULL;
    }

    if (!vmm_owned_add(frame)) {
        (void)pmm_free_frame(frame);
        return false;
    }

    *physical_address = frame;
    *table = mapped_table;
    return true;
}

static bool vmm_release_owned_table(uint64_t physical_address) {
    if (!vmm_owned_contains(physical_address) ||
        !pmm_free_frame(physical_address)) {
        return false;
    }

    return vmm_owned_remove(physical_address);
}

static bool vmm_table_is_empty(const uint64_t *table) {
    uint64_t index;

    if (table == NULL) {
        return false;
    }

    for (index = 0ULL; index < VMM_PAGE_ENTRIES; ++index) {
        if (table[index] != 0ULL) {
            return false;
        }
    }

    return true;
}

static bool vmm_get_or_create_child(uint64_t *entry,
                                    bool large_pages_possible,
                                    bool require_writable,
                                    struct vmm_created_table *created,
                                    uint64_t **child_table) {
    uint64_t child_physical;

    if ((entry == NULL) || (created == NULL) || (child_table == NULL)) {
        return false;
    }

    created->parent_entry = NULL;
    created->physical_address = 0ULL;

    if ((*entry & VMM_ENTRY_PRESENT) != 0ULL) {
        if ((large_pages_possible &&
             ((*entry & VMM_ENTRY_LARGE_PAGE) != 0ULL)) ||
            (require_writable && ((*entry & VMM_ENTRY_WRITABLE) == 0ULL))) {
            return false;
        }

        child_physical = *entry & VMM_ENTRY_ADDRESS_MASK;
        return vmm_table_from_physical(child_physical, child_table);
    }

    if (*entry != 0ULL) {
        return false;
    }

    if (!vmm_allocate_table(&child_physical, child_table)) {
        return false;
    }

    *entry = child_physical | VMM_ENTRY_PRESENT | VMM_ENTRY_WRITABLE;
    created->parent_entry = entry;
    created->physical_address = child_physical;
    return true;
}

static void vmm_rollback_created_tables(struct vmm_created_table *created,
                                        uint64_t created_count,
                                        uintptr_t virtual_address) {
    while (created_count != 0ULL) {
        --created_count;
        if (created[created_count].parent_entry != NULL) {
            *created[created_count].parent_entry = 0ULL;
            x86_64_invalidate_page(virtual_address);
            (void)vmm_release_owned_table(
                created[created_count].physical_address);
        }
    }
}

static enum vmm_lookup_result vmm_walk(uintptr_t virtual_address,
                                       struct vmm_walk_path *path) {
    uint64_t entry_value;

    if ((!vmm_initialized) || (path == NULL) ||
        !vmm_is_page_aligned((uint64_t)virtual_address) ||
        !vmm_is_canonical_4level((uint64_t)virtual_address)) {
        return VMM_LOOKUP_ERROR;
    }

    path->pml4_entry = &vmm_root_table[
        vmm_index_for(virtual_address, 39U)];
    entry_value = *path->pml4_entry;
    if ((entry_value & VMM_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? VMM_LOOKUP_UNMAPPED : VMM_LOOKUP_ERROR;
    }

    path->pdpt_physical = entry_value & VMM_ENTRY_ADDRESS_MASK;
    if (!vmm_table_from_physical(path->pdpt_physical, &path->pdpt)) {
        return VMM_LOOKUP_ERROR;
    }

    path->pdpt_entry = &path->pdpt[
        vmm_index_for(virtual_address, 30U)];
    entry_value = *path->pdpt_entry;
    if ((entry_value & VMM_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? VMM_LOOKUP_UNMAPPED : VMM_LOOKUP_ERROR;
    }
    if ((entry_value & VMM_ENTRY_LARGE_PAGE) != 0ULL) {
        return VMM_LOOKUP_ERROR;
    }

    path->pd_physical = entry_value & VMM_ENTRY_ADDRESS_MASK;
    if (!vmm_table_from_physical(path->pd_physical, &path->pd)) {
        return VMM_LOOKUP_ERROR;
    }

    path->pd_entry = &path->pd[
        vmm_index_for(virtual_address, 21U)];
    entry_value = *path->pd_entry;
    if ((entry_value & VMM_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? VMM_LOOKUP_UNMAPPED : VMM_LOOKUP_ERROR;
    }
    if ((entry_value & VMM_ENTRY_LARGE_PAGE) != 0ULL) {
        return VMM_LOOKUP_ERROR;
    }

    path->pt_physical = entry_value & VMM_ENTRY_ADDRESS_MASK;
    if (!vmm_table_from_physical(path->pt_physical, &path->pt)) {
        return VMM_LOOKUP_ERROR;
    }

    path->pt_entry = &path->pt[
        vmm_index_for(virtual_address, 12U)];
    entry_value = *path->pt_entry;
    if ((entry_value & VMM_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? VMM_LOOKUP_UNMAPPED : VMM_LOOKUP_ERROR;
    }

    return VMM_LOOKUP_MAPPED;
}

static bool vmm_validate_memory_map(
    const struct boring_limine_memmap_response *memory_map) {
    uint64_t index;

    if (memory_map == NULL) {
        VMM_M61_REJECT(0xD3U);
    }
    if (memory_map->entries == NULL) {
        VMM_M61_REJECT(0xD4U);
    }
    if (memory_map->entry_count == 0ULL) {
        VMM_M61_REJECT(0xD5U);
    }
    if (memory_map->entry_count > VMM_MAX_MEMORY_MAP_ENTRIES) {
        VMM_M61_REJECT(0xD6U);
    }

    for (index = 0ULL; index < memory_map->entry_count; ++index) {
        uint64_t end;
        const struct boring_limine_memmap_entry *entry =
            memory_map->entries[index];

        if (entry == NULL) {
            VMM_M61_REJECT(0xD7U);
        }
        if (entry->length == 0ULL) {
            VMM_M61_REJECT(0xD8U);
        }
        if (entry->base > (UINT64_MAX - entry->length)) {
            VMM_M61_REJECT(0xD9U);
        }
        if (!vmm_memory_entry_end(entry, &end)) {
            VMM_M61_REJECT(0xDAU);
        }
    }

    return true;
}

static bool vmm_test_region_clear_of_hhdm(void) {
    uint64_t index;
    uint64_t highest_mapped_physical_end = 0ULL;
    uint64_t hhdm_end;
    uint64_t test_end;

    if (VMM_TEST_REGION_BASE >
        (UINT64_MAX - VMM_TEST_REGION_SIZE)) {
        VMM_M61_REJECT(0xDDU);
    }
    test_end = VMM_TEST_REGION_BASE + VMM_TEST_REGION_SIZE;

    if (!vmm_is_canonical_4level(VMM_TEST_REGION_BASE)) {
        VMM_M61_REJECT(0xDEU);
    }
    if (!vmm_is_canonical_4level(test_end - 1ULL)) {
        VMM_M61_REJECT(0xDFU);
    }
    if (test_end > VMM_KERNEL_HIGHER_HALF_MIN) {
        VMM_M61_REJECT(0xE0U);
    }

    for (index = 0ULL; index < vmm_memory_map->entry_count; ++index) {
        uint64_t entry_end;
        const struct boring_limine_memmap_entry *entry =
            vmm_memory_map->entries[index];

        if (!vmm_memmap_type_has_hhdm(entry->type)) {
            continue;
        }
        if (!vmm_memory_entry_end(entry, &entry_end)) {
            VMM_M61_REJECT(0xE1U);
        }
        if (entry_end > highest_mapped_physical_end) {
            highest_mapped_physical_end = entry_end;
        }
    }

    if (highest_mapped_physical_end == 0ULL) {
        VMM_M61_REJECT(0xE2U);
    }
    if (highest_mapped_physical_end >
        (UINT64_MAX - vmm_hhdm_offset)) {
        VMM_M61_REJECT(0xE3U);
    }
    hhdm_end = vmm_hhdm_offset + highest_mapped_physical_end;

    if ((VMM_TEST_REGION_BASE < hhdm_end) &&
        (vmm_hhdm_offset < test_end)) {
        VMM_M61_REJECT(0xE4U);
    }
    return true;
}

bool vmm_init(const struct boring_limine_hhdm_response *hhdm,
              const struct boring_limine_paging_mode_response *paging_mode,
              const struct boring_limine_memmap_response *memory_map) {
    uint64_t cr3;

    VMM_M61_CLEAR_FAILURE();
    vmm_reset_state();

    if (hhdm == NULL) {
        VMM_M61_REJECT(0xD0U);
    }
    if (paging_mode == NULL) {
        VMM_M61_REJECT(0xD1U);
    }
    if (paging_mode->mode != BORING_LIMINE_PAGING_MODE_X86_64_4LVL) {
        VMM_M61_REJECT(0xD2U);
    }
    if (!vmm_validate_memory_map(memory_map)) {
        return false;
    }
    if (!vmm_is_page_aligned(hhdm->offset)) {
        VMM_M61_REJECT(0xDBU);
    }
    if (!vmm_is_canonical_4level(hhdm->offset)) {
        VMM_M61_REJECT(0xDCU);
    }

    vmm_hhdm_offset = hhdm->offset;
    vmm_memory_map = memory_map;
    vmm_hhdm_ready = true;

    if (!vmm_test_region_clear_of_hhdm()) {
        vmm_reset_state();
        return false;
    }

    cr3 = x86_64_read_cr3();
    vmm_root_physical = cr3 & VMM_ENTRY_ADDRESS_MASK;
    if (vmm_root_physical == 0ULL) {
        vmm_reset_state();
        VMM_M61_REJECT(0xE5U);
    }
    if (!vmm_table_from_physical(vmm_root_physical, &vmm_root_table)) {
        vmm_reset_state();
        VMM_M61_REJECT(0xE6U);
    }

    vmm_initialized = true;
    return true;
}

bool vmm_map_page(uintptr_t virtual_address, uint64_t physical_address,
                  uint64_t flags) {
    struct vmm_created_table created[3];
    uint64_t created_count = 0ULL;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t *pml4_entry;
    uint64_t *pdpt_entry;
    uint64_t *pd_entry;
    uint64_t *pt_entry;
    const bool writable = (flags & VMM_FLAG_WRITABLE) != 0ULL;

    if ((!vmm_initialized) ||
        !vmm_is_page_aligned((uint64_t)virtual_address) ||
        !vmm_is_canonical_4level((uint64_t)virtual_address) ||
        !vmm_physical_supported(physical_address) ||
        !pmm_frame_is_usable(physical_address) ||
        ((flags & ~VMM_ALLOWED_FLAGS) != 0ULL)) {
        return false;
    }

    pml4_entry = &vmm_root_table[vmm_index_for(virtual_address, 39U)];
    if (!vmm_get_or_create_child(pml4_entry, false, writable,
                                 &created[created_count], &pdpt)) {
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    pdpt_entry = &pdpt[vmm_index_for(virtual_address, 30U)];
    if (!vmm_get_or_create_child(pdpt_entry, true, writable,
                                 &created[created_count], &pd)) {
        vmm_rollback_created_tables(created, created_count, virtual_address);
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    pd_entry = &pd[vmm_index_for(virtual_address, 21U)];
    if (!vmm_get_or_create_child(pd_entry, true, writable,
                                 &created[created_count], &pt)) {
        vmm_rollback_created_tables(created, created_count, virtual_address);
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    pt_entry = &pt[vmm_index_for(virtual_address, 12U)];
    if (*pt_entry != 0ULL) {
        vmm_rollback_created_tables(created, created_count, virtual_address);
        return false;
    }

    *pt_entry = physical_address | VMM_ENTRY_PRESENT | flags;
    x86_64_invalidate_page(virtual_address);
    return true;
}

bool vmm_unmap_page(uintptr_t virtual_address) {
    struct vmm_walk_path path;
    enum vmm_lookup_result lookup;

    lookup = vmm_walk(virtual_address, &path);
    if (lookup != VMM_LOOKUP_MAPPED) {
        return false;
    }

    *path.pt_entry = 0ULL;
    x86_64_invalidate_page(virtual_address);

    if (vmm_table_is_empty(path.pt) &&
        vmm_owned_contains(path.pt_physical)) {
        *path.pd_entry = 0ULL;
        x86_64_invalidate_page(virtual_address);
        if (!vmm_release_owned_table(path.pt_physical)) {
            return false;
        }

        if (vmm_table_is_empty(path.pd) &&
            vmm_owned_contains(path.pd_physical)) {
            *path.pdpt_entry = 0ULL;
            x86_64_invalidate_page(virtual_address);
            if (!vmm_release_owned_table(path.pd_physical)) {
                return false;
            }

            if (vmm_table_is_empty(path.pdpt) &&
                vmm_owned_contains(path.pdpt_physical)) {
                *path.pml4_entry = 0ULL;
                x86_64_invalidate_page(virtual_address);
                if (!vmm_release_owned_table(path.pdpt_physical)) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool vmm_translate(uintptr_t virtual_address, uint64_t *physical_address) {
    struct vmm_walk_path path;
    enum vmm_lookup_result lookup;
    uint64_t translated;

    if (physical_address == NULL) {
        return false;
    }

    lookup = vmm_walk(virtual_address, &path);
    if (lookup != VMM_LOOKUP_MAPPED) {
        return false;
    }

    translated = *path.pt_entry & VMM_ENTRY_ADDRESS_MASK;
    if (!vmm_physical_supported(translated)) {
        return false;
    }

    *physical_address = translated;
    return true;
}

bool vmm_get_stats(struct vmm_stats *stats) {
    if ((!vmm_initialized) || (stats == NULL)) {
        return false;
    }

    stats->active_root_physical = vmm_root_physical;
    stats->hhdm_offset = vmm_hhdm_offset;
    stats->owned_page_table_frames = vmm_owned_table_count;
    stats->paging_levels = 4ULL;
    return true;
}

uintptr_t vmm_test_virtual_address(void) {
    return (uintptr_t)VMM_TEST_REGION_BASE;
}
