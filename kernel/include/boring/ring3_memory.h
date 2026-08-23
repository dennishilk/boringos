#ifndef BORING_RING3_MEMORY_H
#define BORING_RING3_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

struct address_space;

bool ring3_user_map_page(struct address_space *space,
                         uintptr_t virtual_address,
                         uint64_t physical_address,
                         bool writable);
bool ring3_user_mapping_valid(const struct address_space *space,
                              uintptr_t virtual_address,
                              uint64_t physical_address,
                              bool writable);

#endif
