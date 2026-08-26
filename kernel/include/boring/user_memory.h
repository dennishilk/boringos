#ifndef BORING_USER_MEMORY_H
#define BORING_USER_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USER_MEMORY_PAGE_SIZE 4096ULL
#define USER_MEMORY_ARENA_BASE 0x0000000100000000ULL
#define USER_MEMORY_ARENA_LIMIT 0x0000000200000000ULL
#define USER_MEMORY_ALLOCATION_MAX 32U
#define USER_MEMORY_BUFFER_HANDLE_MAX 32U
#define USER_MEMORY_BUFFER_MAPPING_MAX 32U
#define USER_MEMORY_BUFFER_OBJECT_MAX 64U
#define USER_MEMORY_ANON_MAX_BYTES (16ULL * 1024ULL * 1024ULL)
#define USER_MEMORY_BUFFER_MAX_BYTES (64ULL * 1024ULL * 1024ULL)
#define USER_MEMORY_HANDLE_INVALID 0U
#define USER_MEMORY_HANDLE_SLOT_BITS 8U
#define USER_MEMORY_HANDLE_SLOT_MASK 0xffU
#define USER_MEMORY_HANDLE_GENERATION_MAX 0x00ffffffU

struct process;

enum user_memory_result {
    USER_MEMORY_RESULT_OK = 0,
    USER_MEMORY_RESULT_INVALID = 1,
    USER_MEMORY_RESULT_NO_SPACE = 2,
    USER_MEMORY_RESULT_NO_MEMORY = 3,
    USER_MEMORY_RESULT_INTERNAL = 4,
    USER_MEMORY_RESULT_NOT_INITIALIZED = 5
};

struct user_memory_allocation {
    uintptr_t base;
    uint32_t page_count;
    bool active;
};

struct user_memory_buffer_handle {
    uint32_t object_index;
    uint32_t generation;
    bool active;
};

struct user_memory_buffer_mapping {
    uintptr_t base;
    uint32_t page_count;
    uint32_t object_index;
    bool active;
};

struct user_memory_process_state {
    struct user_memory_allocation allocations[USER_MEMORY_ALLOCATION_MAX];
    struct user_memory_buffer_handle handles[USER_MEMORY_BUFFER_HANDLE_MAX];
    struct user_memory_buffer_mapping mappings[USER_MEMORY_BUFFER_MAPPING_MAX];
};

/*
 * Kernel-internal M33 grant token. It carries no userspace authority and is
 * never exposed through the ABI. One active token owns exactly one retained
 * reference to an existing M32 buffer backing object.
 */
struct user_buffer_retained_ref {
    uint32_t object_index;
    bool active;
};

/*
 * Receive-side reservation for transactional capability installation. No
 * handle-table mutation occurs until user_buffer_commit_install().
 */
struct user_buffer_install_ticket {
    uint32_t object_index;
    uint32_t generation;
    uint16_t slot;
    bool active;
};

struct user_memory_cleanup_stats {
    uint32_t allocations_released;
    uint32_t mappings_released;
    uint32_t handles_released;
    uint32_t objects_before;
    uint32_t objects_after;
};

struct user_memory_global_stats {
    uint32_t active_objects;
    uint32_t object_limit;
};

bool user_memory_system_init(void);
void user_memory_process_state_init(struct user_memory_process_state *state);
bool user_memory_process_state_empty(const struct user_memory_process_state *state);

enum user_memory_result user_memory_allocate(struct process *process,
                                              size_t size,
                                              uintptr_t *base_out);
enum user_memory_result user_memory_free(struct process *process,
                                          uintptr_t base);
enum user_memory_result user_buffer_create(struct process *process,
                                            size_t size,
                                            uint32_t *handle_out);
enum user_memory_result user_buffer_map(struct process *process,
                                         uint32_t handle,
                                         uintptr_t *base_out);
enum user_memory_result user_buffer_unmap(struct process *process,
                                           uintptr_t base);
enum user_memory_result user_buffer_close(struct process *process,
                                           uint32_t handle);

void user_buffer_retained_ref_clear(struct user_buffer_retained_ref *reference);
bool user_buffer_retained_ref_active(
    const struct user_buffer_retained_ref *reference);
enum user_memory_result user_buffer_retain(
    struct process *process,
    uint32_t handle,
    struct user_buffer_retained_ref *reference_out);
enum user_memory_result user_buffer_release_retained(
    struct user_buffer_retained_ref *reference);
void user_buffer_install_ticket_clear(struct user_buffer_install_ticket *ticket);
enum user_memory_result user_buffer_prepare_install(
    struct process *process,
    const struct user_buffer_retained_ref *reference,
    struct user_buffer_install_ticket *ticket_out,
    uint32_t *handle_out);
enum user_memory_result user_buffer_commit_install(
    struct process *process,
    struct user_buffer_retained_ref *reference,
    struct user_buffer_install_ticket *ticket);

enum user_memory_result user_memory_process_cleanup(
    struct process *process,
    struct user_memory_cleanup_stats *stats);
bool user_memory_get_global_stats(struct user_memory_global_stats *stats);

#endif
