#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/address_space.h>
#include <boring/heap.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/user_memory.h>
#include <boring/vmm.h>

#define USER_MEMORY_OBJECT_NONE UINT32_MAX

struct user_memory_buffer_object {
    uint64_t size_bytes;
    uint64_t *frames;
    uint32_t page_count;
    uint32_t reference_count;
    bool active;
};

static struct user_memory_buffer_object
    buffer_objects[USER_MEMORY_BUFFER_OBJECT_MAX];
static bool user_memory_initialized;

static bool add_u64(uint64_t first, uint64_t second, uint64_t *result) {
    if ((result == NULL) || (second > UINT64_MAX - first)) {
        return false;
    }
    *result = first + second;
    return true;
}

static bool mul_u64(uint64_t first, uint64_t second, uint64_t *result) {
    if ((result == NULL) || ((first != 0ULL) &&
        (second > UINT64_MAX / first))) {
        return false;
    }
    *result = first * second;
    return true;
}

static bool page_count_for_size(size_t size,
                                uint64_t maximum,
                                uint32_t *page_count) {
    uint64_t rounded;
    uint64_t pages;

    if ((page_count == NULL) || (size == 0U) ||
        ((uint64_t)size > maximum) ||
        ((uint64_t)size > UINT64_MAX - (USER_MEMORY_PAGE_SIZE - 1ULL))) {
        return false;
    }
    rounded = (uint64_t)size + (USER_MEMORY_PAGE_SIZE - 1ULL);
    pages = rounded / USER_MEMORY_PAGE_SIZE;
    if ((pages == 0ULL) || (pages > (uint64_t)UINT32_MAX)) {
        return false;
    }
    *page_count = (uint32_t)pages;
    return true;
}

static bool range_bytes(uint32_t page_count, uint64_t *bytes) {
    return (page_count != 0U) &&
           mul_u64((uint64_t)page_count, USER_MEMORY_PAGE_SIZE, bytes);
}

static bool ranges_overlap(uintptr_t first_base,
                           uint64_t first_bytes,
                           uintptr_t second_base,
                           uint64_t second_bytes) {
    uint64_t first_end;
    uint64_t second_end;

    if (!add_u64((uint64_t)first_base, first_bytes, &first_end) ||
        !add_u64((uint64_t)second_base, second_bytes, &second_end)) {
        return true;
    }
    return ((uint64_t)first_base < second_end) &&
           ((uint64_t)second_base < first_end);
}

static bool process_ready(const struct process *process) {
    return user_memory_initialized && (process != NULL) &&
           process_is_alive(process) && !process->address_space.bootstrap &&
           process->address_space.initialized;
}

static void allocation_clear(struct user_memory_allocation *allocation) {
    if (allocation == NULL) {
        return;
    }
    allocation->base = 0U;
    allocation->page_count = 0U;
    allocation->active = false;
}

static void mapping_clear(struct user_memory_buffer_mapping *mapping) {
    if (mapping == NULL) {
        return;
    }
    mapping->base = 0U;
    mapping->page_count = 0U;
    mapping->object_index = USER_MEMORY_OBJECT_NONE;
    mapping->active = false;
}

static uint32_t next_generation(uint32_t generation) {
    if ((generation == 0U) ||
        (generation >= (uint32_t)USER_MEMORY_HANDLE_GENERATION_MAX)) {
        return 1U;
    }
    return generation + 1U;
}

static void handle_reset(struct user_memory_buffer_handle *handle,
                         bool advance_generation) {
    if (handle == NULL) {
        return;
    }
    if (advance_generation) {
        handle->generation = next_generation(handle->generation);
    } else if (handle->generation == 0U) {
        handle->generation = 1U;
    }
    handle->object_index = USER_MEMORY_OBJECT_NONE;
    handle->active = false;
}

static void object_clear(struct user_memory_buffer_object *object) {
    if (object == NULL) {
        return;
    }
    object->size_bytes = 0ULL;
    object->frames = NULL;
    object->page_count = 0U;
    object->reference_count = 0U;
    object->active = false;
}

static bool zero_frame(uint64_t physical_address) {
    uint8_t *bytes;
    size_t index;

    if (!vmm_pmm_frame_to_hhdm(physical_address, (void **)&bytes)) {
        return false;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_PAGE_SIZE; ++index) {
        bytes[index] = 0U;
    }
    return true;
}

static bool range_conflict(const struct user_memory_process_state *state,
                           uintptr_t candidate,
                           uint64_t bytes,
                           uintptr_t *advance_to) {
    size_t index;
    bool conflict = false;
    uint64_t farthest = (uint64_t)candidate;

    if ((state == NULL) || (advance_to == NULL)) {
        return true;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        const struct user_memory_allocation *allocation =
            &state->allocations[index];
        uint64_t occupied_bytes;
        uint64_t occupied_end;

        if (!allocation->active) {
            continue;
        }
        if (!range_bytes(allocation->page_count, &occupied_bytes) ||
            !add_u64((uint64_t)allocation->base, occupied_bytes,
                     &occupied_end)) {
            return true;
        }
        if (ranges_overlap(candidate, bytes, allocation->base,
                           occupied_bytes)) {
            conflict = true;
            if (occupied_end > farthest) {
                farthest = occupied_end;
            }
        }
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        const struct user_memory_buffer_mapping *mapping =
            &state->mappings[index];
        uint64_t occupied_bytes;
        uint64_t occupied_end;

        if (!mapping->active) {
            continue;
        }
        if (!range_bytes(mapping->page_count, &occupied_bytes) ||
            !add_u64((uint64_t)mapping->base, occupied_bytes,
                     &occupied_end)) {
            return true;
        }
        if (ranges_overlap(candidate, bytes, mapping->base, occupied_bytes)) {
            conflict = true;
            if (occupied_end > farthest) {
                farthest = occupied_end;
            }
        }
    }
    *advance_to = (uintptr_t)farthest;
    return conflict;
}

static bool find_virtual_range(const struct user_memory_process_state *state,
                               uint32_t page_count,
                               uintptr_t *base_out) {
    uint64_t bytes;
    uintptr_t candidate = (uintptr_t)USER_MEMORY_ARENA_BASE;
    size_t attempts = 0U;
    const size_t maximum_attempts =
        (size_t)USER_MEMORY_ALLOCATION_MAX +
        (size_t)USER_MEMORY_BUFFER_MAPPING_MAX + 1U;

    if ((state == NULL) || (base_out == NULL) ||
        !range_bytes(page_count, &bytes) ||
        (bytes > USER_MEMORY_ARENA_LIMIT - USER_MEMORY_ARENA_BASE)) {
        return false;
    }

    while (attempts < maximum_attempts) {
        uint64_t end;
        uintptr_t advance_to = candidate;

        if (!add_u64((uint64_t)candidate, bytes, &end) ||
            ((uint64_t)candidate < USER_MEMORY_ARENA_BASE) ||
            (end > USER_MEMORY_ARENA_LIMIT)) {
            return false;
        }
        if (!range_conflict(state, candidate, bytes, &advance_to)) {
            *base_out = candidate;
            return true;
        }
        if (advance_to <= candidate) {
            return false;
        }
        candidate = advance_to;
        ++attempts;
    }
    return false;
}

static struct user_memory_allocation *find_allocation_slot(
    struct user_memory_process_state *state) {
    size_t index;

    if (state == NULL) {
        return NULL;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        if (!state->allocations[index].active) {
            return &state->allocations[index];
        }
    }
    return NULL;
}

static struct user_memory_buffer_mapping *find_mapping_slot(
    struct user_memory_process_state *state) {
    size_t index;

    if (state == NULL) {
        return NULL;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        if (!state->mappings[index].active) {
            return &state->mappings[index];
        }
    }
    return NULL;
}

static struct user_memory_buffer_object *object_at(uint32_t object_index) {
    if ((object_index >= (uint32_t)USER_MEMORY_BUFFER_OBJECT_MAX) ||
        !buffer_objects[object_index].active) {
        return NULL;
    }
    return &buffer_objects[object_index];
}

static struct user_memory_buffer_object *find_object_slot(
    uint32_t *object_index) {
    uint32_t index;

    if (object_index == NULL) {
        return NULL;
    }
    for (index = 0U; index < (uint32_t)USER_MEMORY_BUFFER_OBJECT_MAX; ++index) {
        if (!buffer_objects[index].active) {
            *object_index = index;
            return &buffer_objects[index];
        }
    }
    return NULL;
}

static struct user_memory_buffer_handle *find_handle_slot(
    struct user_memory_process_state *state,
    uint32_t *slot_index) {
    uint32_t index;

    if ((state == NULL) || (slot_index == NULL)) {
        return NULL;
    }
    for (index = 0U; index < (uint32_t)USER_MEMORY_BUFFER_HANDLE_MAX; ++index) {
        if (!state->handles[index].active) {
            *slot_index = index;
            return &state->handles[index];
        }
    }
    return NULL;
}

static bool encode_handle(uint32_t slot_index,
                          uint32_t generation,
                          uint32_t *handle_out) {
    uint32_t encoded;

    if ((handle_out == NULL) ||
        (slot_index >= (uint32_t)USER_MEMORY_BUFFER_HANDLE_MAX) ||
        (generation == 0U) ||
        (generation > (uint32_t)USER_MEMORY_HANDLE_GENERATION_MAX)) {
        return false;
    }
    encoded = (generation << USER_MEMORY_HANDLE_SLOT_BITS) |
              (slot_index + 1U);
    if (encoded == USER_MEMORY_HANDLE_INVALID) {
        return false;
    }
    *handle_out = encoded;
    return true;
}

static struct user_memory_buffer_handle *decode_handle(
    struct user_memory_process_state *state,
    uint32_t encoded,
    uint32_t *slot_index) {
    uint32_t low;
    uint32_t generation;
    struct user_memory_buffer_handle *handle;

    if ((state == NULL) || (encoded == USER_MEMORY_HANDLE_INVALID)) {
        return NULL;
    }
    low = encoded & (uint32_t)USER_MEMORY_HANDLE_SLOT_MASK;
    generation = encoded >> USER_MEMORY_HANDLE_SLOT_BITS;
    if ((low == 0U) ||
        (low > (uint32_t)USER_MEMORY_BUFFER_HANDLE_MAX) ||
        (generation == 0U)) {
        return NULL;
    }
    handle = &state->handles[low - 1U];
    if (!handle->active || (handle->generation != generation) ||
        (object_at(handle->object_index) == NULL)) {
        return NULL;
    }
    if (slot_index != NULL) {
        *slot_index = low - 1U;
    }
    return handle;
}

static void rollback_mappings(struct process *process,
                              uintptr_t base,
                              uint32_t mapped_pages) {
    while (mapped_pages != 0U) {
        --mapped_pages;
        (void)address_space_unmap_page(
            &process->address_space,
            base + (uintptr_t)((uint64_t)mapped_pages *
                               USER_MEMORY_PAGE_SIZE));
    }
}

static void free_frame_vector(uint64_t *frames, uint32_t frame_count) {
    while (frame_count != 0U) {
        --frame_count;
        if (frames[frame_count] != 0ULL) {
            (void)pmm_free_frame(frames[frame_count]);
            frames[frame_count] = 0ULL;
        }
    }
}

static bool object_release_storage(struct user_memory_buffer_object *object) {
    uint32_t index;
    bool ok = true;

    if ((object == NULL) || !object->active ||
        (object->reference_count != 0U) || (object->frames == NULL)) {
        return false;
    }
    for (index = 0U; index < object->page_count; ++index) {
        if ((object->frames[index] == 0ULL) ||
            !pmm_free_frame(object->frames[index])) {
            ok = false;
        }
        object->frames[index] = 0ULL;
    }
    if (!kfree(object->frames)) {
        ok = false;
    }
    object_clear(object);
    return ok;
}

static bool object_unref(uint32_t object_index) {
    struct user_memory_buffer_object *object = object_at(object_index);

    if ((object == NULL) || (object->reference_count == 0U)) {
        return false;
    }
    --object->reference_count;
    if (object->reference_count == 0U) {
        return object_release_storage(object);
    }
    return true;
}

bool user_memory_system_init(void) {
    size_t index;

    if (user_memory_initialized) {
        return true;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_OBJECT_MAX; ++index) {
        object_clear(&buffer_objects[index]);
    }
    user_memory_initialized = true;
    return true;
}

void user_memory_process_state_init(struct user_memory_process_state *state) {
    size_t index;

    if (state == NULL) {
        return;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        allocation_clear(&state->allocations[index]);
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_HANDLE_MAX; ++index) {
        state->handles[index].generation = 1U;
        handle_reset(&state->handles[index], false);
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        mapping_clear(&state->mappings[index]);
    }
}

bool user_memory_process_state_empty(const struct user_memory_process_state *state) {
    size_t index;

    if (state == NULL) {
        return false;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        if (state->allocations[index].active) {
            return false;
        }
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_HANDLE_MAX; ++index) {
        if (state->handles[index].active) {
            return false;
        }
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        if (state->mappings[index].active) {
            return false;
        }
    }
    return true;
}

enum user_memory_result user_memory_allocate(struct process *process,
                                              size_t size,
                                              uintptr_t *base_out) {
    struct user_memory_allocation *slot;
    uint64_t *frames;
    uintptr_t base;
    uint32_t page_count;
    uint32_t allocated = 0U;
    uint32_t mapped = 0U;
    uint32_t index;
    uint64_t vector_bytes;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if ((base_out == NULL) ||
        !page_count_for_size(size, USER_MEMORY_ANON_MAX_BYTES, &page_count)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    slot = find_allocation_slot(&process->user_memory);
    if (slot == NULL) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    if (!find_virtual_range(&process->user_memory, page_count, &base)) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    if (!mul_u64((uint64_t)page_count, sizeof(*frames), &vector_bytes) ||
        (vector_bytes > (uint64_t)SIZE_MAX)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    frames = (uint64_t *)kmalloc((size_t)vector_bytes);
    if (frames == NULL) {
        return USER_MEMORY_RESULT_NO_MEMORY;
    }
    for (index = 0U; index < page_count; ++index) {
        frames[index] = 0ULL;
    }

    for (index = 0U; index < page_count; ++index) {
        const uintptr_t virtual_address =
            base + (uintptr_t)((uint64_t)index * USER_MEMORY_PAGE_SIZE);
        struct ring3_user_mapping_info info;

        if (!pmm_alloc_frame(&frames[index])) {
            break;
        }
        ++allocated;
        if (!zero_frame(frames[index]) ||
            !ring3_user_map_page_permissions(&process->address_space,
                                             virtual_address,
                                             frames[index], true, false)) {
            break;
        }
        ++mapped;
        if (!ring3_user_query_mapping(&process->address_space,
                                      virtual_address, &info) ||
            (info.physical_address != frames[index]) || !info.writable ||
            info.executable) {
            break;
        }
    }
    if ((allocated != page_count) || (mapped != page_count)) {
        rollback_mappings(process, base, mapped);
        free_frame_vector(frames, allocated);
        (void)kfree(frames);
        return USER_MEMORY_RESULT_NO_MEMORY;
    }

    slot->base = base;
    slot->page_count = page_count;
    slot->active = true;
    *base_out = base;
    if (!kfree(frames)) {
        return USER_MEMORY_RESULT_INTERNAL;
    }
    return USER_MEMORY_RESULT_OK;
}

static struct user_memory_allocation *find_allocation_exact(
    struct user_memory_process_state *state,
    uintptr_t base) {
    size_t index;

    if (state == NULL) {
        return NULL;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        if (state->allocations[index].active &&
            (state->allocations[index].base == base)) {
            return &state->allocations[index];
        }
    }
    return NULL;
}

enum user_memory_result user_memory_free(struct process *process,
                                          uintptr_t base) {
    struct user_memory_allocation *allocation;
    uint64_t *frames;
    uint64_t vector_bytes;
    uint32_t index;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if ((base == 0U) || (base < (uintptr_t)USER_MEMORY_ARENA_BASE) ||
        (base >= (uintptr_t)USER_MEMORY_ARENA_LIMIT)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    allocation = find_allocation_exact(&process->user_memory, base);
    if (allocation == NULL) {
        return USER_MEMORY_RESULT_INVALID;
    }
    if (!mul_u64((uint64_t)allocation->page_count, sizeof(*frames),
                 &vector_bytes) || (vector_bytes > (uint64_t)SIZE_MAX)) {
        return USER_MEMORY_RESULT_INTERNAL;
    }
    frames = (uint64_t *)kmalloc((size_t)vector_bytes);
    if (frames == NULL) {
        return USER_MEMORY_RESULT_NO_MEMORY;
    }
    for (index = 0U; index < allocation->page_count; ++index) {
        const uintptr_t virtual_address =
            base + (uintptr_t)((uint64_t)index * USER_MEMORY_PAGE_SIZE);
        struct ring3_user_mapping_info info;

        frames[index] = 0ULL;
        if (!ring3_user_query_mapping(&process->address_space,
                                      virtual_address, &info) ||
            !info.writable || info.executable ||
            ((info.physical_address & (USER_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL)) {
            (void)kfree(frames);
            return USER_MEMORY_RESULT_INTERNAL;
        }
        frames[index] = info.physical_address;
    }
    for (index = 0U; index < allocation->page_count; ++index) {
        const uintptr_t virtual_address =
            base + (uintptr_t)((uint64_t)index * USER_MEMORY_PAGE_SIZE);
        if (!address_space_unmap_page(&process->address_space,
                                      virtual_address)) {
            (void)kfree(frames);
            return USER_MEMORY_RESULT_INTERNAL;
        }
    }
    for (index = 0U; index < allocation->page_count; ++index) {
        if (!pmm_free_frame(frames[index])) {
            (void)kfree(frames);
            return USER_MEMORY_RESULT_INTERNAL;
        }
    }
    allocation_clear(allocation);
    if (!kfree(frames)) {
        return USER_MEMORY_RESULT_INTERNAL;
    }
    return USER_MEMORY_RESULT_OK;
}

enum user_memory_result user_buffer_create(struct process *process,
                                            size_t size,
                                            uint32_t *handle_out) {
    struct user_memory_buffer_object *object;
    struct user_memory_buffer_handle *handle;
    uint64_t *frames;
    uint64_t vector_bytes;
    uint32_t object_index = 0U;
    uint32_t handle_slot = 0U;
    uint32_t page_count;
    uint32_t allocated = 0U;
    uint32_t index;
    uint32_t encoded;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if ((handle_out == NULL) ||
        !page_count_for_size(size, USER_MEMORY_BUFFER_MAX_BYTES, &page_count)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    object = find_object_slot(&object_index);
    handle = find_handle_slot(&process->user_memory, &handle_slot);
    if ((object == NULL) || (handle == NULL)) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    if (!mul_u64((uint64_t)page_count, sizeof(*frames), &vector_bytes) ||
        (vector_bytes > (uint64_t)SIZE_MAX)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    frames = (uint64_t *)kmalloc((size_t)vector_bytes);
    if (frames == NULL) {
        return USER_MEMORY_RESULT_NO_MEMORY;
    }
    for (index = 0U; index < page_count; ++index) {
        frames[index] = 0ULL;
    }
    for (index = 0U; index < page_count; ++index) {
        if (!pmm_alloc_frame(&frames[index])) {
            break;
        }
        ++allocated;
        if (!zero_frame(frames[index])) {
            break;
        }
    }
    if (allocated != page_count) {
        free_frame_vector(frames, allocated);
        (void)kfree(frames);
        return USER_MEMORY_RESULT_NO_MEMORY;
    }
    if (!encode_handle(handle_slot, handle->generation, &encoded)) {
        free_frame_vector(frames, allocated);
        (void)kfree(frames);
        return USER_MEMORY_RESULT_INTERNAL;
    }

    object->size_bytes = (uint64_t)size;
    object->frames = frames;
    object->page_count = page_count;
    object->reference_count = 1U;
    object->active = true;
    handle->object_index = object_index;
    handle->active = true;
    *handle_out = encoded;
    return USER_MEMORY_RESULT_OK;
}

enum user_memory_result user_buffer_map(struct process *process,
                                         uint32_t encoded_handle,
                                         uintptr_t *base_out) {
    struct user_memory_buffer_handle *handle;
    struct user_memory_buffer_object *object;
    struct user_memory_buffer_mapping *mapping;
    uintptr_t base;
    uint32_t mapped = 0U;
    uint32_t index;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if (base_out == NULL) {
        return USER_MEMORY_RESULT_INVALID;
    }
    handle = decode_handle(&process->user_memory, encoded_handle, NULL);
    if (handle == NULL) {
        return USER_MEMORY_RESULT_INVALID;
    }
    object = object_at(handle->object_index);
    mapping = find_mapping_slot(&process->user_memory);
    if ((object == NULL) || (mapping == NULL)) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    if (!find_virtual_range(&process->user_memory, object->page_count, &base)) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    for (index = 0U; index < object->page_count; ++index) {
        const uintptr_t virtual_address =
            base + (uintptr_t)((uint64_t)index * USER_MEMORY_PAGE_SIZE);
        struct ring3_user_mapping_info info;

        if (!ring3_user_map_page_permissions(&process->address_space,
                                             virtual_address,
                                             object->frames[index],
                                             true, false)) {
            break;
        }
        ++mapped;
        if (!ring3_user_query_mapping(&process->address_space,
                                      virtual_address, &info) ||
            (info.physical_address != object->frames[index]) ||
            !info.writable || info.executable) {
            break;
        }
    }
    if (mapped != object->page_count) {
        rollback_mappings(process, base, mapped);
        return USER_MEMORY_RESULT_NO_MEMORY;
    }
    if (object->reference_count == UINT32_MAX) {
        rollback_mappings(process, base, mapped);
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    ++object->reference_count;
    mapping->base = base;
    mapping->page_count = object->page_count;
    mapping->object_index = handle->object_index;
    mapping->active = true;
    *base_out = base;
    return USER_MEMORY_RESULT_OK;
}

static struct user_memory_buffer_mapping *find_mapping_exact(
    struct user_memory_process_state *state,
    uintptr_t base) {
    size_t index;

    if (state == NULL) {
        return NULL;
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        if (state->mappings[index].active &&
            (state->mappings[index].base == base)) {
            return &state->mappings[index];
        }
    }
    return NULL;
}

enum user_memory_result user_buffer_unmap(struct process *process,
                                           uintptr_t base) {
    struct user_memory_buffer_mapping *mapping;
    uint32_t object_index;
    uint32_t index;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if ((base == 0U) || (base < (uintptr_t)USER_MEMORY_ARENA_BASE) ||
        (base >= (uintptr_t)USER_MEMORY_ARENA_LIMIT)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    mapping = find_mapping_exact(&process->user_memory, base);
    if (mapping == NULL) {
        return USER_MEMORY_RESULT_INVALID;
    }
    for (index = 0U; index < mapping->page_count; ++index) {
        const uintptr_t virtual_address =
            base + (uintptr_t)((uint64_t)index * USER_MEMORY_PAGE_SIZE);
        struct ring3_user_mapping_info info;
        struct user_memory_buffer_object *object =
            object_at(mapping->object_index);

        if ((object == NULL) || (index >= object->page_count) ||
            !ring3_user_query_mapping(&process->address_space,
                                      virtual_address, &info) ||
            (info.physical_address != object->frames[index]) ||
            !info.writable || info.executable) {
            return USER_MEMORY_RESULT_INTERNAL;
        }
    }
    for (index = 0U; index < mapping->page_count; ++index) {
        if (!address_space_unmap_page(
                &process->address_space,
                base + (uintptr_t)((uint64_t)index * USER_MEMORY_PAGE_SIZE))) {
            return USER_MEMORY_RESULT_INTERNAL;
        }
    }
    object_index = mapping->object_index;
    mapping_clear(mapping);
    return object_unref(object_index) ? USER_MEMORY_RESULT_OK :
                                        USER_MEMORY_RESULT_INTERNAL;
}

enum user_memory_result user_buffer_close(struct process *process,
                                           uint32_t encoded_handle) {
    struct user_memory_buffer_handle *handle;
    uint32_t object_index;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    handle = decode_handle(&process->user_memory, encoded_handle, NULL);
    if (handle == NULL) {
        return USER_MEMORY_RESULT_INVALID;
    }
    object_index = handle->object_index;
    handle_reset(handle, true);
    return object_unref(object_index) ? USER_MEMORY_RESULT_OK :
                                        USER_MEMORY_RESULT_INTERNAL;
}

static uint32_t active_object_count(void) {
    uint32_t count = 0U;
    uint32_t index;

    for (index = 0U; index < (uint32_t)USER_MEMORY_BUFFER_OBJECT_MAX; ++index) {
        if (buffer_objects[index].active) {
            ++count;
        }
    }
    return count;
}

enum user_memory_result user_memory_process_cleanup(
    struct process *process,
    struct user_memory_cleanup_stats *stats) {
    struct user_memory_cleanup_stats local = { 0U, 0U, 0U, 0U, 0U };
    size_t index;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    local.objects_before = active_object_count();

    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        if (process->user_memory.allocations[index].active) {
            const uintptr_t base = process->user_memory.allocations[index].base;
            const enum user_memory_result result = user_memory_free(process, base);
            if (result != USER_MEMORY_RESULT_OK) {
                return result;
            }
            ++local.allocations_released;
        }
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        if (process->user_memory.mappings[index].active) {
            const uintptr_t base = process->user_memory.mappings[index].base;
            const enum user_memory_result result = user_buffer_unmap(process, base);
            if (result != USER_MEMORY_RESULT_OK) {
                return result;
            }
            ++local.mappings_released;
        }
    }
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_HANDLE_MAX; ++index) {
        struct user_memory_buffer_handle *handle =
            &process->user_memory.handles[index];
        if (handle->active) {
            uint32_t encoded;
            enum user_memory_result result;

            if (!encode_handle((uint32_t)index, handle->generation, &encoded)) {
                return USER_MEMORY_RESULT_INTERNAL;
            }
            result = user_buffer_close(process, encoded);
            if (result != USER_MEMORY_RESULT_OK) {
                return result;
            }
            ++local.handles_released;
        }
    }
    local.objects_after = active_object_count();
    if (!user_memory_process_state_empty(&process->user_memory)) {
        return USER_MEMORY_RESULT_INTERNAL;
    }
    if (stats != NULL) {
        *stats = local;
    }
    return USER_MEMORY_RESULT_OK;
}

bool user_memory_get_global_stats(struct user_memory_global_stats *stats) {
    if (!user_memory_initialized || (stats == NULL)) {
        return false;
    }
    stats->active_objects = active_object_count();
    stats->object_limit = USER_MEMORY_BUFFER_OBJECT_MAX;
    return true;
}
