#ifndef BORING_RING3_MEMORY_H
#define BORING_RING3_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct address_space;

struct ring3_user_mapping_info {
    uint64_t physical_address;
    bool writable;
    bool executable;
};

bool ring3_user_map_page(struct address_space *space,
                         uintptr_t virtual_address,
                         uint64_t physical_address,
                         bool writable);
bool ring3_user_map_page_permissions(struct address_space *space,
                                     uintptr_t virtual_address,
                                     uint64_t physical_address,
                                     bool writable,
                                     bool executable);
bool ring3_user_mapping_valid(const struct address_space *space,
                              uintptr_t virtual_address,
                              uint64_t physical_address,
                              bool writable);
bool ring3_user_mapping_permissions_valid(const struct address_space *space,
                                          uintptr_t virtual_address,
                                          uint64_t physical_address,
                                          bool writable,
                                          bool executable);
bool ring3_user_query_mapping(const struct address_space *space,
                              uintptr_t virtual_address,
                              struct ring3_user_mapping_info *info);
bool ring3_user_translate(const struct address_space *space,
                          uintptr_t virtual_address,
                          bool require_writable,
                          uint64_t *physical_address);
bool ring3_user_range_valid(uintptr_t start, size_t length);
bool ring3_shared_higher_half_supervisor_only(
    const struct address_space *space);

#endif
