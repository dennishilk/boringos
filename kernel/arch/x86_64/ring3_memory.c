#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/address_space.h>
#include <boring/cpu.h>
#include <boring/pmm.h>
#include <boring/ring3_memory.h>
#include <boring/vmm.h>

#define USER_PAGE_ENTRIES 512U
#define USER_ENTRY_PRESENT (1ULL << 0)
#define USER_ENTRY_WRITABLE (1ULL << 1)
#define USER_ENTRY_USER (1ULL << 2)
#define USER_ENTRY_LARGE_PAGE (1ULL << 7)
#define USER_ENTRY_ADDRESS_MASK 0x000ffffffffff000ULL
#define USER_ENTRY_NO_EXECUTE (1ULL << 63)

struct user_created_table {
    uint64_t *parent_entry;
    uint64_t physical_address;
};

static bool page_aligned(uint64_t value) {
    return (value & (VMM_PAGE_SIZE - 1ULL)) == 0ULL;
}

static bool canonical_lower(uintptr_t address) {
    const uint64_t value = (uint64_t)address;

    return ((value >> 48U) == 0ULL) &&
           (((value >> 47U) & 1ULL) == 0ULL) &&
           (((value >> 39U) & 0x1ffULL) <
            (uint64_t)ADDRESS_SPACE_SHARED_PML4_START);
}

bool ring3_user_range_valid(uintptr_t start, size_t length) {
    uintptr_t end;

    if ((length == 0U) || !canonical_lower(start) ||
        ((uintptr_t)(length - 1U) > (UINTPTR_MAX - start))) {
        return false;
    }

    end = start + (uintptr_t)(length - 1U);
    return canonical_lower(end);
}

static uint64_t table_index(uintptr_t address, unsigned int shift) {
    return ((uint64_t)address >> shift) & 0x1ffULL;
}

static bool owned_contains(const struct address_space *space,
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

static bool owned_add(struct address_space *space, uint64_t physical_address) {
    if ((space == NULL) || !space->initialized ||
        (space->owned_table_count >=
         (uint64_t)ADDRESS_SPACE_MAX_OWNED_TABLE_FRAMES) ||
        owned_contains(space, physical_address)) {
        return false;
    }

    space->owned_table_frames[space->owned_table_count] = physical_address;
    ++space->owned_table_count;
    return true;
}

static bool owned_remove(struct address_space *space,
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

static bool table_pointer(uint64_t physical_address, uint64_t **table) {
    struct vmm_stats stats;
    uint64_t virtual_address;

    if ((table == NULL) || !page_aligned(physical_address) ||
        ((physical_address & ~USER_ENTRY_ADDRESS_MASK) != 0ULL) ||
        !vmm_get_stats(&stats) ||
        (physical_address > UINT64_MAX - stats.hhdm_offset)) {
        return false;
    }

    virtual_address = stats.hhdm_offset + physical_address;
    if ((virtual_address >> 48U) != 0xffffULL) {
        return false;
    }

    *table = (uint64_t *)(uintptr_t)virtual_address;
    return true;
}

static bool allocate_user_table(struct address_space *space,
                                uint64_t *physical_address,
                                uint64_t **table) {
    uint64_t frame;
    size_t index;

    if ((space == NULL) || (physical_address == NULL) || (table == NULL) ||
        !pmm_alloc_frame(&frame)) {
        return false;
    }

    if (!pmm_frame_is_usable(frame) || !table_pointer(frame, table)) {
        (void)pmm_free_frame(frame);
        return false;
    }

    for (index = 0U; index < (size_t)USER_PAGE_ENTRIES; ++index) {
        (*table)[index] = 0ULL;
    }

    if (!owned_add(space, frame)) {
        (void)pmm_free_frame(frame);
        return false;
    }

    *physical_address = frame;
    return true;
}

static bool release_created_table(struct address_space *space,
                                  uint64_t physical_address) {
    if (!owned_contains(space, physical_address) ||
        !pmm_free_frame(physical_address)) {
        return false;
    }
    return owned_remove(space, physical_address);
}

static bool get_or_create_user_child(struct address_space *space,
                                     uint64_t *entry,
                                     bool large_page_possible,
                                     struct user_created_table *created,
                                     uint64_t **child) {
    uint64_t physical_address;

    if ((space == NULL) || (entry == NULL) || (created == NULL) ||
        (child == NULL)) {
        return false;
    }

    created->parent_entry = NULL;
    created->physical_address = 0ULL;

    if ((*entry & USER_ENTRY_PRESENT) != 0ULL) {
        if (((*entry & USER_ENTRY_USER) == 0ULL) ||
            (large_page_possible &&
             ((*entry & USER_ENTRY_LARGE_PAGE) != 0ULL))) {
            return false;
        }

        physical_address = *entry & USER_ENTRY_ADDRESS_MASK;
        return owned_contains(space, physical_address) &&
               table_pointer(physical_address, child);
    }

    if (*entry != 0ULL) {
        return false;
    }

    if (!allocate_user_table(space, &physical_address, child)) {
        return false;
    }

    *entry = physical_address | USER_ENTRY_PRESENT |
             USER_ENTRY_WRITABLE | USER_ENTRY_USER;
    created->parent_entry = entry;
    created->physical_address = physical_address;
    return true;
}

static void rollback_created_tables(struct address_space *space,
                                    struct user_created_table *created,
                                    size_t count) {
    while (count != 0U) {
        --count;
        if (created[count].parent_entry != NULL) {
            *created[count].parent_entry = 0ULL;
            (void)release_created_table(space,
                                        created[count].physical_address);
        }
    }
}

/*
 * User access to an x86_64 mapping requires U/S=1 in every paging entry on
 * the translation path. A shared PML4 entry inherited from Limine may carry
 * U/S=1 while a lower entry still makes the effective mapping supervisor-only.
 * Walk only paths whose ancestors remain user-enabled; any leaf reached with
 * user_path=true would be an actual user-accessible higher-half mapping.
 */
static bool shared_table_supervisor_only(const uint64_t *table,
                                         unsigned int level,
                                         bool user_path) {
    size_t index;

    if (table == NULL) {
        return false;
    }

    for (index = 0U; index < (size_t)USER_PAGE_ENTRIES; ++index) {
        const uint64_t entry = table[index];
        const bool entry_user = (entry & USER_ENTRY_USER) != 0ULL;
        const bool next_user_path = user_path && entry_user;
        uint64_t *child;

        if ((entry & USER_ENTRY_PRESENT) == 0ULL) {
            continue;
        }

        if (!next_user_path) {
            continue;
        }

        if (level == 0U) {
            return false;
        }

        if (((level == 2U) || (level == 1U)) &&
            ((entry & USER_ENTRY_LARGE_PAGE) != 0ULL)) {
            return false;
        }

        if (!table_pointer(entry & USER_ENTRY_ADDRESS_MASK, &child) ||
            !shared_table_supervisor_only(child, level - 1U,
                                          next_user_path)) {
            return false;
        }
    }

    return true;
}

bool ring3_user_map_page(struct address_space *space,
                         uintptr_t virtual_address,
                         uint64_t physical_address,
                         bool writable) {
    return ring3_user_map_page_permissions(space, virtual_address,
                                           physical_address, writable, true);
}

bool ring3_user_map_page_permissions(struct address_space *space,
                                     uintptr_t virtual_address,
                                     uint64_t physical_address,
                                     bool writable,
                                     bool executable) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t *entry;
    struct user_created_table created[3];
    size_t created_count = 0U;

    if ((space == NULL) || !space->initialized || space->bootstrap ||
        !canonical_lower(virtual_address) ||
        !page_aligned((uint64_t)virtual_address) ||
        !page_aligned(physical_address) ||
        !pmm_frame_is_usable(physical_address) ||
        (!executable && !x86_64_nx_enabled()) ||
        !owned_contains(space, space->root_physical) ||
        !table_pointer(space->root_physical, &pml4)) {
        return false;
    }

    entry = &pml4[table_index(virtual_address, 39U)];
    if (!get_or_create_user_child(space, entry, false,
                                  &created[created_count], &pdpt)) {
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    entry = &pdpt[table_index(virtual_address, 30U)];
    if (!get_or_create_user_child(space, entry, true,
                                  &created[created_count], &pd)) {
        rollback_created_tables(space, created, created_count);
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    entry = &pd[table_index(virtual_address, 21U)];
    if (!get_or_create_user_child(space, entry, true,
                                  &created[created_count], &pt)) {
        rollback_created_tables(space, created, created_count);
        return false;
    }
    if (created[created_count].parent_entry != NULL) {
        ++created_count;
    }

    entry = &pt[table_index(virtual_address, 12U)];
    if (*entry != 0ULL) {
        rollback_created_tables(space, created, created_count);
        return false;
    }

    *entry = physical_address | USER_ENTRY_PRESENT | USER_ENTRY_USER |
             (writable ? USER_ENTRY_WRITABLE : 0ULL) |
             (executable ? 0ULL : USER_ENTRY_NO_EXECUTE);
    return true;
}

bool ring3_user_query_mapping(const struct address_space *space,
                              uintptr_t virtual_address,
                              struct ring3_user_mapping_info *info) {
    uint64_t *table;
    uint64_t entry;
    bool writable_path = true;
    bool executable_path = true;
    const bool nx_enabled = x86_64_nx_enabled();
    static const unsigned int shifts[4] = { 39U, 30U, 21U, 12U };
    size_t level;

    if ((space == NULL) || (info == NULL) || !space->initialized ||
        space->bootstrap || !canonical_lower(virtual_address) ||
        !table_pointer(space->root_physical, &table)) {
        return false;
    }

    for (level = 0U; level < 4U; ++level) {
        entry = table[table_index(virtual_address, shifts[level])];
        if (((entry & USER_ENTRY_PRESENT) == 0ULL) ||
            ((entry & USER_ENTRY_USER) == 0ULL)) {
            return false;
        }
        if ((!nx_enabled) && ((entry & USER_ENTRY_NO_EXECUTE) != 0ULL)) {
            return false;
        }

        writable_path = writable_path &&
                        ((entry & USER_ENTRY_WRITABLE) != 0ULL);
        if (nx_enabled && ((entry & USER_ENTRY_NO_EXECUTE) != 0ULL)) {
            executable_path = false;
        }

        if (level == 3U) {
            const uint64_t page_physical = entry & USER_ENTRY_ADDRESS_MASK;
            const uint64_t page_offset =
                (uint64_t)virtual_address & (VMM_PAGE_SIZE - 1ULL);

            if (!pmm_frame_is_usable(page_physical)) {
                return false;
            }
            info->physical_address = page_physical + page_offset;
            info->writable = writable_path;
            info->executable = executable_path;
            return true;
        }

        if (((level > 0U) &&
             ((entry & USER_ENTRY_LARGE_PAGE) != 0ULL))) {
            return false;
        }

        if (!owned_contains(space, entry & USER_ENTRY_ADDRESS_MASK) ||
            !table_pointer(entry & USER_ENTRY_ADDRESS_MASK, &table)) {
            return false;
        }
    }

    return false;
}

bool ring3_user_mapping_valid(const struct address_space *space,
                              uintptr_t virtual_address,
                              uint64_t physical_address,
                              bool writable) {
    struct ring3_user_mapping_info info;

    if (!page_aligned((uint64_t)virtual_address) ||
        !page_aligned(physical_address) ||
        !ring3_user_query_mapping(space, virtual_address, &info)) {
        return false;
    }

    return (info.physical_address == physical_address) &&
           (info.writable == writable);
}

bool ring3_user_mapping_permissions_valid(const struct address_space *space,
                                          uintptr_t virtual_address,
                                          uint64_t physical_address,
                                          bool writable,
                                          bool executable) {
    struct ring3_user_mapping_info info;

    if (!page_aligned((uint64_t)virtual_address) ||
        !page_aligned(physical_address) ||
        !ring3_user_query_mapping(space, virtual_address, &info)) {
        return false;
    }

    return (info.physical_address == physical_address) &&
           (info.writable == writable) &&
           (info.executable == executable);
}

bool ring3_user_translate(const struct address_space *space,
                          uintptr_t virtual_address,
                          bool require_writable,
                          uint64_t *physical_address) {
    struct ring3_user_mapping_info info;

    if ((physical_address == NULL) ||
        !ring3_user_query_mapping(space, virtual_address, &info) ||
        (require_writable && !info.writable)) {
        return false;
    }

    *physical_address = info.physical_address;
    return true;
}

bool ring3_shared_higher_half_supervisor_only(
    const struct address_space *space) {
    uint64_t *root;
    size_t index;

    if ((space == NULL) || !space->initialized || space->bootstrap ||
        !address_space_kernel_mappings_valid(space) ||
        !table_pointer(space->root_physical, &root)) {
        return false;
    }

    for (index = (size_t)ADDRESS_SPACE_SHARED_PML4_START;
         index < (size_t)ADDRESS_SPACE_PML4_ENTRY_COUNT; ++index) {
        const uint64_t entry = root[index];
        uint64_t *pdpt;

        if ((entry & USER_ENTRY_PRESENT) == 0ULL) {
            continue;
        }

        if ((entry & USER_ENTRY_USER) == 0ULL) {
            continue;
        }

        if (!table_pointer(entry & USER_ENTRY_ADDRESS_MASK, &pdpt) ||
            !shared_table_supervisor_only(pdpt, 2U, true)) {
            return false;
        }
    }

    return true;
}
