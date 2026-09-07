#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boring/address_space.h>
#include <boring/heap.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/user_memory.h>
#include <boring/vmm.h>

#define HOST_FRAME_COUNT 512U
#define HOST_MAPPING_COUNT 4096U

struct host_frame {
    uint8_t bytes[USER_MEMORY_PAGE_SIZE];
    bool used;
};

struct host_mapping {
    struct address_space *space;
    uintptr_t virtual_address;
    uint64_t physical_address;
    bool writable;
    bool executable;
    bool active;
};

static struct host_frame host_frames[HOST_FRAME_COUNT];
static struct host_mapping host_mappings[HOST_MAPPING_COUNT];
static int pmm_fail_countdown = -1;
static int map_fail_countdown = -1;
static int kmalloc_fail_countdown = -1;

static void fail(const char *message) {
    (void)fprintf(stderr, "memory-host-test: FAIL: %s\n", message);
    exit(1);
}

static void require(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static size_t active_frame_count(void) {
    size_t index;
    size_t count = 0U;

    for (index = 0U; index < (size_t)HOST_FRAME_COUNT; ++index) {
        if (host_frames[index].used) {
            ++count;
        }
    }
    return count;
}

static size_t active_mapping_count(void) {
    size_t index;
    size_t count = 0U;

    for (index = 0U; index < (size_t)HOST_MAPPING_COUNT; ++index) {
        if (host_mappings[index].active) {
            ++count;
        }
    }
    return count;
}

static void host_reset(void) {
    (void)memset(host_frames, 0, sizeof(host_frames));
    (void)memset(host_mappings, 0, sizeof(host_mappings));
    pmm_fail_countdown = -1;
    map_fail_countdown = -1;
    kmalloc_fail_countdown = -1;
}

static void process_reset(struct process *process, uint64_t pid) {
    if (process == NULL) {
        fail("process_reset null");
    }
    (void)memset(process, 0, sizeof(*process));
    process->pid = pid;
    process->state = PROCESS_ALIVE;
    process->slot_used = true;
    process->address_space.initialized = true;
    process->address_space.bootstrap = false;
    process->address_space.root_physical = 0x1000ULL + (pid * 0x1000ULL);
    user_memory_process_state_init(&process->user_memory);
}

bool process_is_alive(const struct process *process) {
    return (process != NULL) && process->slot_used &&
           (process->state == PROCESS_ALIVE);
}

void *kmalloc(size_t size) {
    if (kmalloc_fail_countdown == 0) {
        return NULL;
    }
    if (kmalloc_fail_countdown > 0) {
        --kmalloc_fail_countdown;
    }
    return malloc(size);
}

bool kfree(void *pointer) {
    if (pointer == NULL) {
        return false;
    }
    free(pointer);
    return true;
}

bool pmm_alloc_frame(uint64_t *physical_address) {
    size_t index;

    if (physical_address == NULL) {
        return false;
    }
    if (pmm_fail_countdown == 0) {
        return false;
    }
    if (pmm_fail_countdown > 0) {
        --pmm_fail_countdown;
    }
    for (index = 0U; index < (size_t)HOST_FRAME_COUNT; ++index) {
        if (!host_frames[index].used) {
            host_frames[index].used = true;
            *physical_address = ((uint64_t)index + 1ULL) * USER_MEMORY_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

bool pmm_free_frame(uint64_t physical_address) {
    uint64_t frame_number;
    size_t index;

    if ((physical_address == 0ULL) ||
        ((physical_address & (USER_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL)) {
        return false;
    }
    frame_number = physical_address / USER_MEMORY_PAGE_SIZE;
    if ((frame_number == 0ULL) ||
        (frame_number > (uint64_t)HOST_FRAME_COUNT)) {
        return false;
    }
    index = (size_t)(frame_number - 1ULL);
    if (!host_frames[index].used) {
        return false;
    }
    host_frames[index].used = false;
    return true;
}

bool pmm_frame_is_usable(uint64_t physical_address) {
    const uint64_t frame_number = physical_address / USER_MEMORY_PAGE_SIZE;
    return (physical_address != 0ULL) &&
           ((physical_address & (USER_MEMORY_PAGE_SIZE - 1ULL)) == 0ULL) &&
           (frame_number >= 1ULL) &&
           (frame_number <= (uint64_t)HOST_FRAME_COUNT);
}

bool vmm_pmm_frame_to_hhdm(uint64_t physical_address, void **virtual_address) {
    const uint64_t frame_number = physical_address / USER_MEMORY_PAGE_SIZE;
    size_t index;

    if ((virtual_address == NULL) || !pmm_frame_is_usable(physical_address)) {
        return false;
    }
    index = (size_t)(frame_number - 1ULL);
    if (!host_frames[index].used) {
        return false;
    }
    *virtual_address = &host_frames[index].bytes[0];
    return true;
}

static struct host_mapping *mapping_find(struct address_space *space,
                                         uintptr_t virtual_address) {
    const uintptr_t page =
        virtual_address & ~(uintptr_t)(USER_MEMORY_PAGE_SIZE - 1ULL);
    size_t index;

    for (index = 0U; index < (size_t)HOST_MAPPING_COUNT; ++index) {
        if (host_mappings[index].active &&
            (host_mappings[index].space == space) &&
            (host_mappings[index].virtual_address == page)) {
            return &host_mappings[index];
        }
    }
    return NULL;
}

bool ring3_user_map_page_permissions(struct address_space *space,
                                     uintptr_t virtual_address,
                                     uint64_t physical_address,
                                     bool writable,
                                     bool executable) {
    size_t index;

    if ((space == NULL) || !space->initialized || space->bootstrap ||
        ((virtual_address & (uintptr_t)(USER_MEMORY_PAGE_SIZE - 1ULL)) != 0U) ||
        !pmm_frame_is_usable(physical_address) ||
        (mapping_find(space, virtual_address) != NULL)) {
        return false;
    }
    if (map_fail_countdown == 0) {
        return false;
    }
    if (map_fail_countdown > 0) {
        --map_fail_countdown;
    }
    for (index = 0U; index < (size_t)HOST_MAPPING_COUNT; ++index) {
        if (!host_mappings[index].active) {
            host_mappings[index].space = space;
            host_mappings[index].virtual_address = virtual_address;
            host_mappings[index].physical_address = physical_address;
            host_mappings[index].writable = writable;
            host_mappings[index].executable = executable;
            host_mappings[index].active = true;
            return true;
        }
    }
    return false;
}

bool ring3_user_query_mapping(const struct address_space *space,
                              uintptr_t virtual_address,
                              struct ring3_user_mapping_info *info) {
    struct host_mapping *mapping;
    const uintptr_t page =
        virtual_address & ~(uintptr_t)(USER_MEMORY_PAGE_SIZE - 1ULL);
    const uint64_t offset =
        (uint64_t)(virtual_address - page);

    if ((space == NULL) || (info == NULL)) {
        return false;
    }
    mapping = mapping_find((struct address_space *)space, page);
    if (mapping == NULL) {
        return false;
    }
    info->physical_address = mapping->physical_address + offset;
    info->writable = mapping->writable;
    info->executable = mapping->executable;
    return true;
}

bool address_space_unmap_page(struct address_space *space,
                              uintptr_t virtual_address) {
    struct host_mapping *mapping = mapping_find(space, virtual_address);

    if (mapping == NULL) {
        return false;
    }
    (void)memset(mapping, 0, sizeof(*mapping));
    return true;
}

static uint8_t *host_user_pointer(struct process *process,
                                  uintptr_t virtual_address) {
    struct ring3_user_mapping_info info;
    uint64_t page_physical;
    uint64_t frame_number;
    size_t frame_index;

    if (!ring3_user_query_mapping(&process->address_space, virtual_address,
                                  &info)) {
        return NULL;
    }
    page_physical = info.physical_address & ~(USER_MEMORY_PAGE_SIZE - 1ULL);
    frame_number = page_physical / USER_MEMORY_PAGE_SIZE;
    if ((frame_number == 0ULL) ||
        (frame_number > (uint64_t)HOST_FRAME_COUNT)) {
        return NULL;
    }
    frame_index = (size_t)(frame_number - 1ULL);
    return &host_frames[frame_index].bytes[
        (size_t)(info.physical_address - page_physical)];
}

static uint64_t host_mapping_physical(struct process *process,
                                      uintptr_t virtual_address) {
    struct ring3_user_mapping_info info;

    if (!ring3_user_query_mapping(&process->address_space, virtual_address,
                                  &info)) {
        return 0ULL;
    }
    return info.physical_address;
}

static void test_anonymous_zero_reuse_and_permissions(void) {
    struct process process;
    uintptr_t first = 0U;
    uintptr_t second = 0U;
    uint64_t first_physical;
    size_t index;

    host_reset();
    process_reset(&process, 1ULL);
    require(user_memory_allocate(&process, 1U, &first) == USER_MEMORY_RESULT_OK,
            "allocate one byte");
    require(first == (uintptr_t)USER_MEMORY_ARENA_BASE,
            "first-fit arena base");
    require(active_mapping_count() == 1U, "one mapping created");
    first_physical = host_mapping_physical(&process, first);
    require(first_physical != 0ULL, "physical backing exists");
    for (index = 0U; index < (size_t)USER_MEMORY_PAGE_SIZE; ++index) {
        uint8_t *byte = host_user_pointer(&process, first + (uintptr_t)index);
        require((byte != NULL) && (*byte == 0U), "new anonymous page zero");
        *byte = 0xa5U;
    }
    {
        struct ring3_user_mapping_info info;
        require(ring3_user_query_mapping(&process.address_space, first, &info),
                "query anonymous mapping");
        require(info.writable && !info.executable,
                "anonymous mapping is RW+NX");
    }
    require(user_memory_free(&process, first) == USER_MEMORY_RESULT_OK,
            "free anonymous page");
    require(active_mapping_count() == 0U, "anonymous unmap complete");
    require(active_frame_count() == 0U, "anonymous frame reclaimed");
    require(user_memory_free(&process, first) == USER_MEMORY_RESULT_INVALID,
            "anonymous double free rejected");

    require(user_memory_allocate(&process, 1U, &second) == USER_MEMORY_RESULT_OK,
            "reallocate one byte");
    require(second == first, "virtual hole reused");
    require(host_mapping_physical(&process, second) == first_physical,
            "physical frame deterministically reused");
    for (index = 0U; index < (size_t)USER_MEMORY_PAGE_SIZE; ++index) {
        const uint8_t *byte = host_user_pointer(&process,
                                                second + (uintptr_t)index);
        require((byte != NULL) && (*byte == 0U),
                "reused anonymous frame zeroed before exposure");
    }
    require(user_memory_free(&process, second) == USER_MEMORY_RESULT_OK,
            "free reused anonymous page");
}

static void test_rounding_holes_bounds_and_slots(void) {
    struct process process;
    uintptr_t first = 0U;
    uintptr_t middle = 0U;
    uintptr_t last = 0U;
    uintptr_t reused = 0U;
    uintptr_t slots[USER_MEMORY_ALLOCATION_MAX];
    size_t index;

    host_reset();
    process_reset(&process, 2ULL);
    require(user_memory_allocate(&process, 1U, &first) == USER_MEMORY_RESULT_OK,
            "first fragmented allocation");
    require(user_memory_allocate(&process, 4097U, &middle) ==
                USER_MEMORY_RESULT_OK,
            "round 4097 to two pages");
    require(user_memory_allocate(&process, 1U, &last) == USER_MEMORY_RESULT_OK,
            "last fragmented allocation");
    require(middle == first + (uintptr_t)USER_MEMORY_PAGE_SIZE,
            "contiguous first-fit placement");
    require(last == middle + (uintptr_t)(2ULL * USER_MEMORY_PAGE_SIZE),
            "rounded range occupied two pages");
    require(user_memory_free(&process, middle) == USER_MEMORY_RESULT_OK,
            "free middle hole");
    require(user_memory_allocate(&process, 4097U, &reused) ==
                USER_MEMORY_RESULT_OK,
            "reuse two-page hole");
    require(reused == middle, "first-fit hole reuse");
    require(user_memory_free(&process, first) == USER_MEMORY_RESULT_OK,
            "free first fragmented");
    require(user_memory_free(&process, last) == USER_MEMORY_RESULT_OK,
            "free last fragmented");
    require(user_memory_free(&process, reused) == USER_MEMORY_RESULT_OK,
            "free reused fragmented");

    require(user_memory_allocate(&process, 0U, &first) ==
                USER_MEMORY_RESULT_INVALID,
            "zero anonymous size rejected");
    require(user_memory_allocate(&process,
                (size_t)USER_MEMORY_ANON_MAX_BYTES + 1U, &first) ==
                USER_MEMORY_RESULT_INVALID,
            "anonymous maximum enforced");
    require(user_memory_allocate(&process, SIZE_MAX, &first) ==
                USER_MEMORY_RESULT_INVALID,
            "anonymous overflow rejected");
    require(user_memory_free(&process, 0U) == USER_MEMORY_RESULT_INVALID,
            "null anonymous free rejected");
    require(user_memory_free(&process,
                (uintptr_t)USER_MEMORY_ARENA_BASE + 123U) ==
                USER_MEMORY_RESULT_INVALID,
            "interior anonymous pointer rejected");

    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        require(user_memory_allocate(&process, 1U, &slots[index]) ==
                    USER_MEMORY_RESULT_OK,
                "fill anonymous metadata slots");
    }
    require(user_memory_allocate(&process, 1U, &first) ==
                USER_MEMORY_RESULT_NO_SPACE,
            "anonymous metadata bound enforced");
    for (index = 0U; index < (size_t)USER_MEMORY_ALLOCATION_MAX; ++index) {
        require(user_memory_free(&process, slots[index]) ==
                    USER_MEMORY_RESULT_OK,
                "release anonymous metadata slot");
    }
    require(active_frame_count() == 0U, "slot test frames reclaimed");
}

static void test_anonymous_failure_rollback(void) {
    struct process process;
    uintptr_t base = 0U;
    const size_t frames_before = active_frame_count();
    const size_t mappings_before = active_mapping_count();

    host_reset();
    process_reset(&process, 3ULL);
    pmm_fail_countdown = 2;
    require(user_memory_allocate(&process, 4U * (size_t)USER_MEMORY_PAGE_SIZE,
                                 &base) == USER_MEMORY_RESULT_NO_MEMORY,
            "partial PMM allocation fails transactionally");
    require(active_frame_count() == frames_before,
            "PMM failure frames rolled back");
    require(active_mapping_count() == mappings_before,
            "PMM failure mappings rolled back");
    require(user_memory_process_state_empty(&process.user_memory),
            "PMM failure metadata rolled back");

    pmm_fail_countdown = -1;
    map_fail_countdown = 2;
    require(user_memory_allocate(&process, 4U * (size_t)USER_MEMORY_PAGE_SIZE,
                                 &base) == USER_MEMORY_RESULT_NO_MEMORY,
            "partial mapping allocation fails transactionally");
    require(active_frame_count() == frames_before,
            "map failure frames rolled back");
    require(active_mapping_count() == mappings_before,
            "map failure aliases rolled back");
    require(user_memory_process_state_empty(&process.user_memory),
            "map failure metadata rolled back");

    map_fail_countdown = -1;
    kmalloc_fail_countdown = 0;
    require(user_memory_allocate(&process, USER_MEMORY_PAGE_SIZE, &base) ==
                USER_MEMORY_RESULT_NO_MEMORY,
            "temporary metadata allocation failure clean");
    require(active_frame_count() == frames_before,
            "kmalloc failure allocated no frame");
}

static void test_shared_alias_lifetime_zero_and_stale_handle(void) {
    struct process process;
    uint32_t first_handle = 0U;
    uint32_t second_handle = 0U;
    uintptr_t alias_a = 0U;
    uintptr_t alias_b = 0U;
    uintptr_t second_alias = 0U;
    uint64_t first_physical;
    size_t index;
    struct user_memory_global_stats stats;

    host_reset();
    process_reset(&process, 4ULL);
    require(user_buffer_create(&process, 3U * (size_t)USER_MEMORY_PAGE_SIZE,
                               &first_handle) == USER_MEMORY_RESULT_OK,
            "create three-page shared buffer");
    require(first_handle != USER_MEMORY_HANDLE_INVALID, "valid local handle");
    require(user_buffer_map(&process, first_handle, &alias_a) ==
                USER_MEMORY_RESULT_OK,
            "map alias A");
    require(user_buffer_map(&process, first_handle, &alias_b) ==
                USER_MEMORY_RESULT_OK,
            "map alias B");
    require(alias_a != alias_b, "aliases have distinct virtual bases");
    for (index = 0U; index < 3U; ++index) {
        const uintptr_t offset =
            (uintptr_t)((uint64_t)index * USER_MEMORY_PAGE_SIZE);
        require(host_mapping_physical(&process, alias_a + offset) ==
                host_mapping_physical(&process, alias_b + offset),
                "aliases share identical physical backing");
        {
            struct ring3_user_mapping_info info;
            require(ring3_user_query_mapping(&process.address_space,
                                              alias_a + offset, &info),
                    "query shared mapping");
            require(info.writable && !info.executable,
                    "shared mapping is RW+NX");
        }
    }
    first_physical = host_mapping_physical(&process, alias_a);
    require(first_physical != 0ULL, "shared physical backing present");
    require(*host_user_pointer(&process, alias_a + 123U) == 0U,
            "new shared buffer zero");
    *host_user_pointer(&process, alias_a + 123U) = 0x5aU;
    *host_user_pointer(&process, alias_a +
        (uintptr_t)USER_MEMORY_PAGE_SIZE + 17U) = 0xc3U;
    require(*host_user_pointer(&process, alias_b + 123U) == 0x5aU,
            "alias byte visible without kernel copy");
    require(*host_user_pointer(&process, alias_b +
        (uintptr_t)USER_MEMORY_PAGE_SIZE + 17U) == 0xc3U,
            "multi-page alias byte visible");

    require(user_buffer_unmap(&process, alias_a) == USER_MEMORY_RESULT_OK,
            "unmap alias A");
    require(host_user_pointer(&process, alias_a + 123U) == NULL,
            "alias A removed");
    require(*host_user_pointer(&process, alias_b + 123U) == 0x5aU,
            "alias B survives A unmap");
    require(user_buffer_close(&process, first_handle) == USER_MEMORY_RESULT_OK,
            "close handle while alias B alive");
    require(user_buffer_map(&process, first_handle, &alias_a) ==
                USER_MEMORY_RESULT_INVALID,
            "closed stale handle rejected");
    require(*host_user_pointer(&process, alias_b + 123U) == 0x5aU,
            "mapping survives handle close");
    require(user_buffer_unmap(&process, alias_b) == USER_MEMORY_RESULT_OK,
            "final mapping unmap");
    require(user_memory_get_global_stats(&stats) &&
            (stats.active_objects == 0U),
            "final reference destroys object");
    require(active_frame_count() == 0U, "buffer backing frames reclaimed");

    require(user_buffer_create(&process, USER_MEMORY_PAGE_SIZE,
                               &second_handle) == USER_MEMORY_RESULT_OK,
            "recreate buffer after destruction");
    require(second_handle != first_handle, "handle generation changes on reuse");
    require(user_buffer_map(&process, first_handle, &alias_a) ==
                USER_MEMORY_RESULT_INVALID,
            "old generation remains stale");
    require(user_buffer_map(&process, second_handle, &second_alias) ==
                USER_MEMORY_RESULT_OK,
            "map recreated buffer");
    require(host_mapping_physical(&process, second_alias) == first_physical,
            "destroyed shared frame deterministically reused");
    for (index = 0U; index < (size_t)USER_MEMORY_PAGE_SIZE; ++index) {
        const uint8_t *byte = host_user_pointer(&process,
            second_alias + (uintptr_t)index);
        require((byte != NULL) && (*byte == 0U),
                "recreated shared frame zeroed before mapping");
    }
    require(user_buffer_unmap(&process, second_alias) == USER_MEMORY_RESULT_OK,
            "unmap recreated buffer");
    require(user_buffer_close(&process, second_handle) == USER_MEMORY_RESULT_OK,
            "close recreated buffer");
    require(active_frame_count() == 0U, "recreated buffer reclaimed");
}

static void test_shared_failures_and_bounds(void) {
    struct process process;
    uint32_t handle = 0U;
    uintptr_t alias = 0U;
    uintptr_t aliases[USER_MEMORY_BUFFER_MAPPING_MAX];
    uint32_t handles[USER_MEMORY_BUFFER_HANDLE_MAX];
    size_t index;

    host_reset();
    process_reset(&process, 5ULL);
    require(user_buffer_create(&process, 0U, &handle) ==
                USER_MEMORY_RESULT_INVALID,
            "zero shared buffer rejected");
    require(user_buffer_create(&process,
                (size_t)USER_MEMORY_BUFFER_MAX_BYTES + 1U, &handle) ==
                USER_MEMORY_RESULT_INVALID,
            "shared maximum enforced");
    require(user_buffer_create(&process, SIZE_MAX, &handle) ==
                USER_MEMORY_RESULT_INVALID,
            "shared size overflow rejected");
    require(user_buffer_map(&process, 0U, &alias) ==
                USER_MEMORY_RESULT_INVALID,
            "invalid zero handle map rejected");
    require(user_buffer_close(&process, 0U) == USER_MEMORY_RESULT_INVALID,
            "invalid zero handle close rejected");
    require(user_buffer_unmap(&process, 0U) == USER_MEMORY_RESULT_INVALID,
            "null shared unmap rejected");

    pmm_fail_countdown = 2;
    require(user_buffer_create(&process, 4U * (size_t)USER_MEMORY_PAGE_SIZE,
                               &handle) == USER_MEMORY_RESULT_NO_MEMORY,
            "buffer create partial PMM failure rolls back");
    require(active_frame_count() == 0U,
            "buffer create failure frames reclaimed");
    pmm_fail_countdown = -1;

    require(user_buffer_create(&process, 4U * (size_t)USER_MEMORY_PAGE_SIZE,
                               &handle) == USER_MEMORY_RESULT_OK,
            "create buffer for map rollback");
    map_fail_countdown = 2;
    require(user_buffer_map(&process, handle, &alias) ==
                USER_MEMORY_RESULT_NO_MEMORY,
            "partial alias map fails transactionally");
    require(active_mapping_count() == 0U,
            "partial alias mapping rolled back");
    map_fail_countdown = -1;
    require(user_buffer_map(&process, handle, &alias) == USER_MEMORY_RESULT_OK,
            "object remains usable after map rollback");
    require(user_buffer_unmap(&process, alias + 1U) ==
                USER_MEMORY_RESULT_INVALID,
            "interior shared mapping pointer rejected");
    require(user_buffer_unmap(&process, alias) == USER_MEMORY_RESULT_OK,
            "valid shared mapping unmaps");
    require(user_buffer_close(&process, handle) == USER_MEMORY_RESULT_OK,
            "close map rollback object");
    require(user_buffer_close(&process, handle) == USER_MEMORY_RESULT_INVALID,
            "double close rejected");

    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_HANDLE_MAX; ++index) {
        require(user_buffer_create(&process, USER_MEMORY_PAGE_SIZE,
                                   &handles[index]) == USER_MEMORY_RESULT_OK,
                "fill process-local handle table");
    }
    require(user_buffer_create(&process, USER_MEMORY_PAGE_SIZE, &handle) ==
                USER_MEMORY_RESULT_NO_SPACE,
            "process-local handle bound enforced");
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_HANDLE_MAX; ++index) {
        require(user_buffer_close(&process, handles[index]) ==
                    USER_MEMORY_RESULT_OK,
                "release bounded handle");
    }

    require(user_buffer_create(&process, USER_MEMORY_PAGE_SIZE, &handle) ==
                USER_MEMORY_RESULT_OK,
            "create mapping-bound object");
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        require(user_buffer_map(&process, handle, &aliases[index]) ==
                    USER_MEMORY_RESULT_OK,
                "fill process-local mapping table");
    }
    require(user_buffer_map(&process, handle, &alias) ==
                USER_MEMORY_RESULT_NO_SPACE,
            "process-local mapping bound enforced");
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_MAPPING_MAX; ++index) {
        require(user_buffer_unmap(&process, aliases[index]) ==
                    USER_MEMORY_RESULT_OK,
                "release bounded mapping");
    }
    require(user_buffer_close(&process, handle) == USER_MEMORY_RESULT_OK,
            "close mapping-bound object");
    require(active_frame_count() == 0U, "bounds test frames reclaimed");
}

static void test_global_object_bound_and_cleanup(void) {
    struct process first;
    struct process second;
    struct process third;
    uint32_t first_handles[USER_MEMORY_BUFFER_HANDLE_MAX];
    uint32_t second_handles[USER_MEMORY_BUFFER_HANDLE_MAX];
    uint32_t extra = 0U;
    size_t index;
    struct user_memory_global_stats global;
    struct user_memory_cleanup_stats cleanup;
    uintptr_t anonymous = 0U;
    uintptr_t mapping = 0U;
    uint32_t handle = 0U;

    host_reset();
    process_reset(&first, 10ULL);
    process_reset(&second, 11ULL);
    process_reset(&third, 12ULL);
    for (index = 0U; index < (size_t)USER_MEMORY_BUFFER_HANDLE_MAX; ++index) {
        require(user_buffer_create(&first, USER_MEMORY_PAGE_SIZE,
                                   &first_handles[index]) ==
                    USER_MEMORY_RESULT_OK,
                "first process fills global objects");
        require(user_buffer_create(&second, USER_MEMORY_PAGE_SIZE,
                                   &second_handles[index]) ==
                    USER_MEMORY_RESULT_OK,
                "second process fills global objects");
    }
    require(user_memory_get_global_stats(&global), "read global buffer stats");
    require(global.active_objects == USER_MEMORY_BUFFER_OBJECT_MAX,
            "global object table reaches fixed bound");
    require(global.object_limit == USER_MEMORY_BUFFER_OBJECT_MAX,
            "global object limit reported");
    require(user_buffer_create(&third, USER_MEMORY_PAGE_SIZE, &extra) ==
                USER_MEMORY_RESULT_NO_SPACE,
            "global object bound enforced");
    require(user_memory_process_cleanup(&first, NULL) == USER_MEMORY_RESULT_OK,
            "cleanup first global-object owner");
    require(user_memory_process_cleanup(&second, NULL) == USER_MEMORY_RESULT_OK,
            "cleanup second global-object owner");
    require(user_memory_get_global_stats(&global) &&
            (global.active_objects == 0U),
            "global objects reclaimed after cleanup");

    require(user_memory_allocate(&third, 2U * (size_t)USER_MEMORY_PAGE_SIZE,
                                 &anonymous) == USER_MEMORY_RESULT_OK,
            "teardown anonymous allocation");
    require(user_buffer_create(&third, 2U * (size_t)USER_MEMORY_PAGE_SIZE,
                               &handle) == USER_MEMORY_RESULT_OK,
            "teardown shared buffer");
    require(user_buffer_map(&third, handle, &mapping) == USER_MEMORY_RESULT_OK,
            "teardown shared mapping");
    require(user_memory_process_cleanup(&third, &cleanup) ==
                USER_MEMORY_RESULT_OK,
            "process exit cleanup succeeds");
    require(cleanup.allocations_released == 1U,
            "cleanup released anonymous allocation");
    require(cleanup.mappings_released == 1U,
            "cleanup released shared mapping");
    require(cleanup.handles_released == 1U,
            "cleanup released shared handle");
    require(cleanup.objects_before == 1U && cleanup.objects_after == 0U,
            "cleanup released final shared object");
    require(user_memory_process_state_empty(&third.user_memory),
            "cleanup leaves process metadata empty");
    require(active_mapping_count() == 0U, "cleanup leaves no mappings");
    require(active_frame_count() == 0U, "cleanup reclaims all backing frames");
}

int main(void) {
    require(user_memory_system_init(), "initialize M32 memory system");
    test_anonymous_zero_reuse_and_permissions();
    test_rounding_holes_bounds_and_slots();
    test_anonymous_failure_rollback();
    test_shared_alias_lifetime_zero_and_stale_handle();
    test_shared_failures_and_bounds();
    test_global_object_bound_and_cleanup();
    (void)puts("M32 userspace memory/shared-buffer host tests passed.");
    return 0;
}
