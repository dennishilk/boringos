#ifndef BORING_ADDRESS_SPACE_H
#define BORING_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stdint.h>

#define ADDRESS_SPACE_MAX_OWNED_TABLE_FRAMES 16U
#define ADDRESS_SPACE_SHARED_PML4_START 256U
#define ADDRESS_SPACE_PML4_ENTRY_COUNT 512U
#define ADDRESS_SPACE_TEST_VIRTUAL_ADDRESS 0x0000004000000000ULL

struct address_space {
    uint64_t root_physical;
    uint64_t owned_table_frames[ADDRESS_SPACE_MAX_OWNED_TABLE_FRAMES];
    uint64_t owned_table_count;
    bool bootstrap;
    bool initialized;
};

struct address_space_stats {
    uint64_t bootstrap_root_physical;
    uint64_t current_root_physical;
    uint64_t created_address_spaces;
    uint64_t destroyed_address_spaces;
    uint64_t address_space_switches;
    uint16_t shared_pml4_start;
};

bool address_space_system_init(struct address_space *bootstrap_space);
bool address_space_create(struct address_space *space);
bool address_space_activate(struct address_space *space);
bool address_space_map_page(struct address_space *space,
                            uintptr_t virtual_address,
                            uint64_t physical_address,
                            uint64_t flags);
bool address_space_unmap_page(struct address_space *space,
                              uintptr_t virtual_address);
bool address_space_translate(const struct address_space *space,
                             uintptr_t virtual_address,
                             uint64_t *physical_address);
bool address_space_kernel_mappings_valid(const struct address_space *space);
bool address_space_destroy(struct address_space *space);
bool address_space_get_stats(struct address_space_stats *stats);
bool address_space_is_active(const struct address_space *space);
uintptr_t address_space_test_virtual_address(void);

#endif
