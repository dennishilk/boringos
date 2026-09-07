#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/address_space.h>
#include <boring/cpu.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define ADDRESS_SPACE_PAGE_ENTRIES 512ULL
#define ADDRESS_SPACE_ENTRY_PRESENT (1ULL << 0)
#define ADDRESS_SPACE_ENTRY_WRITABLE (1ULL << 1)
#define ADDRESS_SPACE_ENTRY_LARGE_PAGE (1ULL << 7)
#define ADDRESS_SPACE_ENTRY_ADDRESS_MASK 0x000ffffffffff000ULL
#define ADDRESS_SPACE_ALLOWED_FLAGS VMM_FLAG_WRITABLE
#define ADDRESS_SPACE_HHDM_PML4_INDEX 256U
#define ADDRESS_SPACE_HEAP_PML4_INDEX 510U
#define ADDRESS_SPACE_KERNEL_PML4_INDEX 511U

struct address_space_created_table {
    uint64_t *parent_entry;
    uint64_t physical_address;
};

struct address_space_walk_path {
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

enum address_space_lookup_result {
    ADDRESS_SPACE_LOOKUP_ERROR = 0,
    ADDRESS_SPACE_LOOKUP_UNMAPPED = 1,
    ADDRESS_SPACE_LOOKUP_MAPPED = 2
};

static struct address_space *bootstrap_address_space;
static struct address_space *current_address_space;
static uint64_t bootstrap_root_physical;
static uint64_t hhdm_offset;
static uint64_t created_address_space_count;
static uint64_t destroyed_address_space_count;
static uint64_t address_space_switch_count;
static bool address_space_initialized;

static bool address_space_page_aligned(uint64_t value) {
    return (value & (VMM_PAGE_SIZE - 1ULL)) == 0ULL;
}

static bool address_space_canonical(uint64_t address) {
    const uint64_t upper = address >> 48U;
    const bool high_half = ((address >> 47U) & 1ULL) != 0ULL;

    return high_half ? (upper == 0xffffULL) : (upper == 0ULL);
}

static bool address_space_physical_supported(uint64_t physical_address) {
    return address_space_page_aligned(physical_address) &&
           ((physical_address & ~ADDRESS_SPACE_ENTRY_ADDRESS_MASK) == 0ULL);
}

static uint64_t address_space_pml4_index(uintptr_t virtual_address) {
    return (((uint64_t)virtual_address >> 39U) & 0x1ffULL);
}

static uint64_t address_space_index(uintptr_t virtual_address,
                                    unsigned int shift) {
    return (((uint64_t)virtual_address >> shift) & 0x1ffULL);
}

static void address_space_clear(struct address_space *space) {
    size_t index;

    if (space == NULL) {
        return;
    }

    space->root_physical = 0ULL;
    space->owned_table_count = 0ULL;
    space->bootstrap = false;
    space->initialized = false;
    for (index = 0U;
         index < (size_t)ADDRESS_SPACE_MAX_OWNED_TABLE_FRAMES;
         ++index) {
        space->owned_table_frames[index] = 0ULL;
    }
}

static bool address_space_table_pointer(uint64_t physical_address,
                                        uint64_t **table) {
    uint64_t virtual_address;

    if ((table == NULL) || !address_space_physical_supported(physical_address) ||
        (physical_address > (UINT64_MAX - hhdm_offset))) {
        return false;
    }

    virtual_address = hhdm_offset + physical_address;
    if (!address_space_canonical(virtual_address)) {
        return false;
    }

    *table = (uint64_t *)(uintptr_t)virtual_address;
    return true;
}

static bool address_space_owned_contains(const struct address_space *space,
                                         uint64_t physical_address) {
    uint64_t index;

    if ((space == NULL) || !space->initialized) {
        return false;
    }

    for (index = 0ULL; index < space->owned_table_count; ++index) {
        if (space->owned_table_frames[index] == physical_address) {
            return true;
        }
    }
    return false;
}

static bool address_space_owned_add(struct address_space *space,
                                    uint64_t physical_address) {
    if ((space == NULL) || !space->initialized ||
        (space->owned_table_count >=
         (uint64_t)ADDRESS_SPACE_MAX_OWNED_TABLE_FRAMES) ||
        address_space_owned_contains(space, physical_address)) {
        return false;
    }

    space->owned_table_frames[space->owned_table_count] = physical_address;
    ++space->owned_table_count;
    return true;
}

static bool address_space_owned_remove(struct address_space *space,
                                       uint64_t physical_address) {
    uint64_t index;

    if ((space == NULL) || !space->initialized) {
        return false;
    }

    for (index = 0ULL; index < space->owned_table_count; ++index) {
        if (space->owned_table_frames[index] == physical_address) {
            uint64_t move_index;

            for (move_index = index + 1ULL;
                 move_index < space->owned_table_count; ++move_index) {
                space->owned_table_frames[move_index - 1ULL] =
                    space->owned_table_frames[move_index];
            }

            --space->owned_table_count;
            space->owned_table_frames[space->owned_table_count] = 0ULL;
            return true;
        }
    }
    return false;
}

static bool address_space_allocate_table(struct address_space *space,
                                         uint64_t *physical_address,
                                         uint64_t **table) {
    uint64_t frame;
    uint64_t *mapped_table;
    size_t index;

    if ((space == NULL) || (physical_address == NULL) || (table == NULL) ||
        !space->initialized ||
        (space->owned_table_count >=
         (uint64_t)ADDRESS_SPACE_MAX_OWNED_TABLE_FRAMES) ||
        !pmm_alloc_frame(&frame)) {
        return false;
    }

    if (!address_space_physical_supported(frame) ||
        !pmm_frame_is_usable(frame) ||
        !address_space_table_pointer(frame, &mapped_table)) {
        (void)pmm_free_frame(frame);
        return false;
    }

    for (index = 0U; index < (size_t)ADDRESS_SPACE_PAGE_ENTRIES; ++index) {
        mapped_table[index] = 0ULL;
    }

    if (!address_space_owned_add(space, frame)) {
        (void)pmm_free_frame(frame);
        return false;
    }

    *physical_address = frame;
    *table = mapped_table;
    return true;
}

static bool address_space_release_owned_table(struct address_space *space,
                                              uint64_t physical_address) {
    if (!address_space_owned_contains(space, physical_address) ||
        !pmm_free_frame(physical_address)) {
        return false;
    }
    return address_space_owned_remove(space, physical_address);
}

static bool address_space_table_empty(const uint64_t *table) {
    size_t index;

    if (table == NULL) {
        return false;
    }

    for (index = 0U; index < (size_t)ADDRESS_SPACE_PAGE_ENTRIES; ++index) {
        if (table[index] != 0ULL) {
            return false;
        }
    }
    return true;
}

static void address_space_invalidate_if_active(
    const struct address_space *space,
    uintptr_t virtual_address) {
    if (address_space_is_active(space)) {
        x86_64_invalidate_page(virtual_address);
    }
}

static bool address_space_get_or_create_child(
    struct address_space *space,
    uint64_t *entry,
    bool large_pages_possible,
    struct address_space_created_table *created,
    uint64_t **child_table) {
    uint64_t child_physical;

    if ((space == NULL) || (entry == NULL) || (created == NULL) ||
        (child_table == NULL)) {
        return false;
    }

    created->parent_entry = NULL;
    created->physical_address = 0ULL;

    if ((*entry & ADDRESS_SPACE_ENTRY_PRESENT) != 0ULL) {
        if ((large_pages_possible &&
             ((*entry & ADDRESS_SPACE_ENTRY_LARGE_PAGE) != 0ULL))) {
            return false;
        }
        child_physical = *entry & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
        if (!address_space_owned_contains(space, child_physical)) {
            return false;
        }
        return address_space_table_pointer(child_physical, child_table);
    }

    if (*entry != 0ULL) {
        return false;
    }

    if (!address_space_allocate_table(space, &child_physical, child_table)) {
        return false;
    }

    *entry = child_physical | ADDRESS_SPACE_ENTRY_PRESENT |
             ADDRESS_SPACE_ENTRY_WRITABLE;
    created->parent_entry = entry;
    created->physical_address = child_physical;
    return true;
}

static void address_space_rollback_created(
    struct address_space *space,
    struct address_space_created_table *created,
    uint64_t created_count,
    uintptr_t virtual_address) {
    while (created_count != 0ULL) {
        --created_count;
        if (created[created_count].parent_entry != NULL) {
            *created[created_count].parent_entry = 0ULL;
            address_space_invalidate_if_active(space, virtual_address);
            (void)address_space_release_owned_table(
                space, created[created_count].physical_address);
        }
    }
}

static enum address_space_lookup_result address_space_walk(
    const struct address_space *space,
    uintptr_t virtual_address,
    struct address_space_walk_path *path) {
    uint64_t *root;
    uint64_t entry_value;
    const uint64_t pml4_index = address_space_pml4_index(virtual_address);

    if ((!address_space_initialized) || (space == NULL) ||
        (!space->initialized) || (path == NULL) ||
        !address_space_page_aligned((uint64_t)virtual_address) ||
        !address_space_canonical((uint64_t)virtual_address) ||
        (pml4_index >= (uint64_t)ADDRESS_SPACE_SHARED_PML4_START) ||
        !address_space_table_pointer(space->root_physical, &root)) {
        return ADDRESS_SPACE_LOOKUP_ERROR;
    }

    path->pml4_entry = &root[pml4_index];
    entry_value = *path->pml4_entry;
    if ((entry_value & ADDRESS_SPACE_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? ADDRESS_SPACE_LOOKUP_UNMAPPED :
                                      ADDRESS_SPACE_LOOKUP_ERROR;
    }

    path->pdpt_physical = entry_value & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if (!address_space_owned_contains(space, path->pdpt_physical) ||
        !address_space_table_pointer(path->pdpt_physical, &path->pdpt)) {
        return ADDRESS_SPACE_LOOKUP_ERROR;
    }

    path->pdpt_entry = &path->pdpt[address_space_index(virtual_address, 30U)];
    entry_value = *path->pdpt_entry;
    if ((entry_value & ADDRESS_SPACE_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? ADDRESS_SPACE_LOOKUP_UNMAPPED :
                                      ADDRESS_SPACE_LOOKUP_ERROR;
    }
    if ((entry_value & ADDRESS_SPACE_ENTRY_LARGE_PAGE) != 0ULL) {
        return ADDRESS_SPACE_LOOKUP_ERROR;
    }

    path->pd_physical = entry_value & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if (!address_space_owned_contains(space, path->pd_physical) ||
        !address_space_table_pointer(path->pd_physical, &path->pd)) {
        return ADDRESS_SPACE_LOOKUP_ERROR;
    }

    path->pd_entry = &path->pd[address_space_index(virtual_address, 21U)];
    entry_value = *path->pd_entry;
    if ((entry_value & ADDRESS_SPACE_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? ADDRESS_SPACE_LOOKUP_UNMAPPED :
                                      ADDRESS_SPACE_LOOKUP_ERROR;
    }
    if ((entry_value & ADDRESS_SPACE_ENTRY_LARGE_PAGE) != 0ULL) {
        return ADDRESS_SPACE_LOOKUP_ERROR;
    }

    path->pt_physical = entry_value & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if (!address_space_owned_contains(space, path->pt_physical) ||
        !address_space_table_pointer(path->pt_physical, &path->pt)) {
        return ADDRESS_SPACE_LOOKUP_ERROR;
    }

    path->pt_entry = &path->pt[address_space_index(virtual_address, 12U)];
    entry_value = *path->pt_entry;
    if ((entry_value & ADDRESS_SPACE_ENTRY_PRESENT) == 0ULL) {
        return (entry_value == 0ULL) ? ADDRESS_SPACE_LOOKUP_UNMAPPED :
                                      ADDRESS_SPACE_LOOKUP_ERROR;
    }

    return ADDRESS_SPACE_LOOKUP_MAPPED;
}

bool address_space_system_init(struct address_space *bootstrap_space) {
    struct vmm_stats vmm_stats;
    uint64_t *root;
    uint64_t active_root;

    if (address_space_initialized || (bootstrap_space == NULL) ||
        !vmm_get_stats(&vmm_stats)) {
        return false;
    }

    active_root = x86_64_read_cr3() & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if ((active_root == 0ULL) ||
        (active_root != vmm_stats.active_root_physical) ||
        !address_space_page_aligned(vmm_stats.hhdm_offset) ||
        !address_space_canonical(vmm_stats.hhdm_offset)) {
        return false;
    }

    hhdm_offset = vmm_stats.hhdm_offset;
    bootstrap_root_physical = active_root;
    address_space_clear(bootstrap_space);
    bootstrap_space->root_physical = active_root;
    bootstrap_space->bootstrap = true;
    bootstrap_space->initialized = true;

    if (!address_space_table_pointer(active_root, &root) ||
        ((root[ADDRESS_SPACE_HHDM_PML4_INDEX] &
          ADDRESS_SPACE_ENTRY_PRESENT) == 0ULL) ||
        ((root[ADDRESS_SPACE_HEAP_PML4_INDEX] &
          ADDRESS_SPACE_ENTRY_PRESENT) == 0ULL) ||
        ((root[ADDRESS_SPACE_KERNEL_PML4_INDEX] &
          ADDRESS_SPACE_ENTRY_PRESENT) == 0ULL)) {
        address_space_clear(bootstrap_space);
        bootstrap_root_physical = 0ULL;
        hhdm_offset = 0ULL;
        return false;
    }

    bootstrap_address_space = bootstrap_space;
    current_address_space = bootstrap_space;
    created_address_space_count = 0ULL;
    destroyed_address_space_count = 0ULL;
    address_space_switch_count = 0ULL;
    address_space_initialized = true;
    return true;
}

bool address_space_create(struct address_space *space) {
    uint64_t *bootstrap_root;
    uint64_t *root;
    uint64_t root_physical;
    size_t index;

    if ((!address_space_initialized) || (space == NULL) || space->initialized ||
        (bootstrap_address_space == NULL) ||
        !address_space_table_pointer(bootstrap_root_physical,
                                     &bootstrap_root)) {
        return false;
    }

    address_space_clear(space);
    space->initialized = true;
    if (!address_space_allocate_table(space, &root_physical, &root)) {
        address_space_clear(space);
        return false;
    }
    space->root_physical = root_physical;

    for (index = (size_t)ADDRESS_SPACE_SHARED_PML4_START;
         index < (size_t)ADDRESS_SPACE_PML4_ENTRY_COUNT; ++index) {
        root[index] = bootstrap_root[index];
    }

    if (!address_space_kernel_mappings_valid(space)) {
        (void)pmm_free_frame(root_physical);
        address_space_clear(space);
        return false;
    }

    ++created_address_space_count;
    return true;
}

bool address_space_activate(struct address_space *space) {
    uint64_t *root;
    uint64_t active_root;

    if ((!address_space_initialized) || (space == NULL) ||
        !space->initialized || (space->root_physical == 0ULL) ||
        !address_space_table_pointer(space->root_physical, &root) ||
        !address_space_kernel_mappings_valid(space)) {
        return false;
    }
    (void)root;

    if ((!space->bootstrap) &&
        !address_space_owned_contains(space, space->root_physical)) {
        return false;
    }

    active_root = x86_64_read_cr3() & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if (active_root == space->root_physical) {
        current_address_space = space;
        return true;
    }

    x86_64_write_cr3(space->root_physical);
    active_root = x86_64_read_cr3() & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if (active_root != space->root_physical) {
        return false;
    }

    current_address_space = space;
    ++address_space_switch_count;
    return true;
}

bool address_space_map_page(struct address_space *space,
                            uintptr_t virtual_address,
                            uint64_t physical_address,
                            uint64_t flags) {
    struct address_space_created_table created[3];
    uint64_t created_count = 0ULL;
    uint64_t *root;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t *pml4_entry;
    uint64_t *pdpt_entry;
    uint64_t *pd_entry;
    uint64_t *pt_entry;
    const uint64_t pml4_index = address_space_pml4_index(virtual_address);

    if ((!address_space_initialized) || (space == NULL) ||
        !space->initialized || space->bootstrap ||
        !address_space_page_aligned((uint64_t)virtual_address) ||
        !address_space_canonical((uint64_t)virtual_address) ||
        (pml4_index >= (uint64_t)ADDRESS_SPACE_SHARED_PML4_START) ||
        !address_space_physical_supported(physical_address) ||
        !pmm_frame_is_usable(physical_address) ||
        ((flags & ~ADDRESS_SPACE_ALLOWED_FLAGS) != 0ULL) ||
        !address_space_table_pointer(space->root_physical, &root)) {
        return false;
    }

    pml4_entry = &root[pml4_index];
    if (!address_space_get_or_create_child(
            space, pml4_entry, false, &created[created_count], &pdpt)) {
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    pdpt_entry = &pdpt[address_space_index(virtual_address, 30U)];
    if (!address_space_get_or_create_child(
            space, pdpt_entry, true, &created[created_count], &pd)) {
        address_space_rollback_created(space, created, created_count,
                                       virtual_address);
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    pd_entry = &pd[address_space_index(virtual_address, 21U)];
    if (!address_space_get_or_create_child(
            space, pd_entry, true, &created[created_count], &pt)) {
        address_space_rollback_created(space, created, created_count,
                                       virtual_address);
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    pt_entry = &pt[address_space_index(virtual_address, 12U)];
    if (*pt_entry != 0ULL) {
        address_space_rollback_created(space, created, created_count,
                                       virtual_address);
        return false;
    }

    *pt_entry = physical_address | ADDRESS_SPACE_ENTRY_PRESENT | flags;
    address_space_invalidate_if_active(space, virtual_address);
    return true;
}

bool address_space_unmap_page(struct address_space *space,
                              uintptr_t virtual_address) {
    struct address_space_walk_path path;
    enum address_space_lookup_result lookup;

    if ((space == NULL) || space->bootstrap) {
        return false;
    }

    lookup = address_space_walk(space, virtual_address, &path);
    if (lookup != ADDRESS_SPACE_LOOKUP_MAPPED) {
        return false;
    }

    *path.pt_entry = 0ULL;
    address_space_invalidate_if_active(space, virtual_address);

    if (address_space_table_empty(path.pt) &&
        address_space_owned_contains(space, path.pt_physical)) {
        *path.pd_entry = 0ULL;
        if (!address_space_release_owned_table(space, path.pt_physical)) {
            return false;
        }

        if (address_space_table_empty(path.pd) &&
            address_space_owned_contains(space, path.pd_physical)) {
            *path.pdpt_entry = 0ULL;
            if (!address_space_release_owned_table(space, path.pd_physical)) {
                return false;
            }

            if (address_space_table_empty(path.pdpt) &&
                address_space_owned_contains(space, path.pdpt_physical)) {
                *path.pml4_entry = 0ULL;
                if (!address_space_release_owned_table(
                        space, path.pdpt_physical)) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool address_space_translate(const struct address_space *space,
                             uintptr_t virtual_address,
                             uint64_t *physical_address) {
    struct address_space_walk_path path;
    enum address_space_lookup_result lookup;
    uint64_t translated;

    if (physical_address == NULL) {
        return false;
    }

    lookup = address_space_walk(space, virtual_address, &path);
    if (lookup != ADDRESS_SPACE_LOOKUP_MAPPED) {
        return false;
    }

    translated = *path.pt_entry & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if (!address_space_physical_supported(translated)) {
        return false;
    }

    *physical_address = translated;
    return true;
}

bool address_space_kernel_mappings_valid(const struct address_space *space) {
    uint64_t *bootstrap_root;
    uint64_t *root;
    size_t index;

    if ((!address_space_initialized) || (space == NULL) ||
        !space->initialized || (bootstrap_address_space == NULL) ||
        !address_space_table_pointer(bootstrap_root_physical,
                                     &bootstrap_root) ||
        !address_space_table_pointer(space->root_physical, &root)) {
        return false;
    }

    for (index = (size_t)ADDRESS_SPACE_SHARED_PML4_START;
         index < (size_t)ADDRESS_SPACE_PML4_ENTRY_COUNT; ++index) {
        if (root[index] != bootstrap_root[index]) {
            return false;
        }
    }

    return ((root[ADDRESS_SPACE_HHDM_PML4_INDEX] &
             ADDRESS_SPACE_ENTRY_PRESENT) != 0ULL) &&
           ((root[ADDRESS_SPACE_HEAP_PML4_INDEX] &
             ADDRESS_SPACE_ENTRY_PRESENT) != 0ULL) &&
           ((root[ADDRESS_SPACE_KERNEL_PML4_INDEX] &
             ADDRESS_SPACE_ENTRY_PRESENT) != 0ULL);
}

bool address_space_destroy(struct address_space *space) {
    uint64_t *root;
    size_t index;

    if ((!address_space_initialized) || (space == NULL) ||
        !space->initialized || space->bootstrap ||
        address_space_is_active(space) ||
        !address_space_kernel_mappings_valid(space) ||
        !address_space_table_pointer(space->root_physical, &root)) {
        return false;
    }

    for (index = 0U; index < (size_t)ADDRESS_SPACE_SHARED_PML4_START;
         ++index) {
        if (root[index] != 0ULL) {
            return false;
        }
    }

    if ((space->owned_table_count != 1ULL) ||
        !address_space_owned_contains(space, space->root_physical) ||
        !pmm_free_frame(space->root_physical)) {
        return false;
    }

    address_space_clear(space);
    ++destroyed_address_space_count;
    return true;
}

bool address_space_get_stats(struct address_space_stats *stats) {
    uint64_t current_root;

    if ((!address_space_initialized) || (stats == NULL) ||
        (bootstrap_address_space == NULL) ||
        (current_address_space == NULL)) {
        return false;
    }

    current_root = x86_64_read_cr3() & ADDRESS_SPACE_ENTRY_ADDRESS_MASK;
    if (current_root != current_address_space->root_physical) {
        return false;
    }

    stats->bootstrap_root_physical = bootstrap_root_physical;
    stats->current_root_physical = current_root;
    stats->created_address_spaces = created_address_space_count;
    stats->destroyed_address_spaces = destroyed_address_space_count;
    stats->address_space_switches = address_space_switch_count;
    stats->shared_pml4_start = (uint16_t)ADDRESS_SPACE_SHARED_PML4_START;
    return true;
}

bool address_space_is_active(const struct address_space *space) {
    if ((!address_space_initialized) || (space == NULL) ||
        !space->initialized) {
        return false;
    }

    return ((x86_64_read_cr3() & ADDRESS_SPACE_ENTRY_ADDRESS_MASK) ==
            space->root_physical);
}

uintptr_t address_space_test_virtual_address(void) {
    return (uintptr_t)ADDRESS_SPACE_TEST_VIRTUAL_ADDRESS;
}
