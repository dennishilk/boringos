#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/heap.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define HEAP_PAGE_SIZE ((size_t)VMM_PAGE_SIZE)
#define HEAP_INITIAL_PAGES 2U
#define HEAP_REGION_SIZE ((size_t)0x01000000U)
#define HEAP_MAX_PAGES (HEAP_REGION_SIZE / HEAP_PAGE_SIZE)
#define HEAP_VIRTUAL_BASE ((uintptr_t)0xffffff0000200000ULL)
#define HEAP_VIRTUAL_LIMIT (HEAP_VIRTUAL_BASE + (uintptr_t)HEAP_REGION_SIZE)
#define HEAP_BLOCK_MAGIC 0x424f52494e474850ULL
#define HEAP_STATE_FREE 0x46524545424c4f43ULL
#define HEAP_STATE_USED 0x55534544424c4f43ULL
#define HEAP_GUARD_SEED 0x9e3779b97f4a7c15ULL

struct heap_block {
    uint64_t magic;
    size_t size;
    struct heap_block *prev;
    struct heap_block *next;
    uint64_t state;
    uint64_t guard;
};

_Static_assert((sizeof(struct heap_block) % 16U) == 0U,
               "heap block header must preserve 16-byte alignment");
_Static_assert(VMM_PAGE_SIZE == PMM_PAGE_SIZE,
               "PMM and VMM page sizes must match");

static struct heap_block *heap_first;
static size_t heap_mapped_pages;
static bool heap_initialized;
static bool heap_corrupted;

static void heap_reset_state(void) {
    heap_first = NULL;
    heap_mapped_pages = 0U;
    heap_initialized = false;
    heap_corrupted = false;
}

static bool heap_mapped_end(uintptr_t *end) {
    size_t mapped_bytes;

    if ((end == NULL) || (heap_mapped_pages > HEAP_MAX_PAGES)) {
        return false;
    }

    mapped_bytes = heap_mapped_pages * HEAP_PAGE_SIZE;
    if (HEAP_VIRTUAL_BASE > (UINTPTR_MAX - (uintptr_t)mapped_bytes)) {
        return false;
    }

    *end = HEAP_VIRTUAL_BASE + (uintptr_t)mapped_bytes;
    return *end <= HEAP_VIRTUAL_LIMIT;
}

static uint64_t heap_guard_for(const struct heap_block *block) {
    return HEAP_GUARD_SEED ^
           (uint64_t)(uintptr_t)block ^
           (uint64_t)block->size ^
           (uint64_t)(uintptr_t)block->prev ^
           (uint64_t)(uintptr_t)block->next ^
           block->state;
}

static void heap_refresh_block(struct heap_block *block) {
    block->magic = HEAP_BLOCK_MAGIC;
    block->guard = heap_guard_for(block);
}

static bool heap_block_valid(const struct heap_block *block) {
    uintptr_t mapped_end;
    uintptr_t block_address;
    uintptr_t payload_address;

    if ((block == NULL) || !heap_mapped_end(&mapped_end)) {
        return false;
    }

    block_address = (uintptr_t)block;
    if ((block_address < HEAP_VIRTUAL_BASE) ||
        (block_address > (mapped_end - sizeof(struct heap_block))) ||
        ((block_address & ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL)) {
        return false;
    }

    if ((block->magic != HEAP_BLOCK_MAGIC) ||
        ((block->state != HEAP_STATE_FREE) &&
         (block->state != HEAP_STATE_USED)) ||
        (block->size < (size_t)KERNEL_HEAP_ALIGNMENT) ||
        ((block->size % (size_t)KERNEL_HEAP_ALIGNMENT) != 0U) ||
        (block->guard != heap_guard_for(block))) {
        return false;
    }

    if (block_address > (UINTPTR_MAX - sizeof(struct heap_block))) {
        return false;
    }
    payload_address = block_address + sizeof(struct heap_block);

    return block->size <= (size_t)(mapped_end - payload_address);
}

static bool heap_validate(void) {
    const struct heap_block *current;
    const struct heap_block *previous = NULL;
    uintptr_t mapped_end;

    if ((!heap_initialized) || heap_corrupted ||
        (heap_first == NULL) ||
        ((uintptr_t)heap_first != HEAP_VIRTUAL_BASE) ||
        !heap_mapped_end(&mapped_end)) {
        return false;
    }

    current = heap_first;
    while (current != NULL) {
        uintptr_t block_address;
        uintptr_t payload_address;
        uintptr_t block_end;

        if (!heap_block_valid(current) || (current->prev != previous)) {
            return false;
        }

        if ((previous != NULL) &&
            ((previous->next != current) ||
             ((previous->state == HEAP_STATE_FREE) &&
              (current->state == HEAP_STATE_FREE)))) {
            return false;
        }

        block_address = (uintptr_t)current;
        payload_address = block_address + sizeof(struct heap_block);
        block_end = payload_address + (uintptr_t)current->size;

        if (current->next != NULL) {
            if ((uintptr_t)current->next != block_end) {
                return false;
            }
        } else if (block_end != mapped_end) {
            return false;
        }

        previous = current;
        current = current->next;
    }

    return true;
}

static void heap_zero_page(uintptr_t virtual_address) {
    size_t index;
    volatile uint8_t *bytes = (volatile uint8_t *)virtual_address;

    for (index = 0U; index < HEAP_PAGE_SIZE; ++index) {
        bytes[index] = 0U;
    }
}

static void heap_rollback_initial_pages(size_t mapped_count,
                                        const uint64_t *frames) {
    while (mapped_count != 0U) {
        uintptr_t virtual_address;

        --mapped_count;
        virtual_address = HEAP_VIRTUAL_BASE +
                          ((uintptr_t)mapped_count *
                           (uintptr_t)HEAP_PAGE_SIZE);
        (void)vmm_unmap_page(virtual_address);
        (void)pmm_free_frame(frames[mapped_count]);
    }
}

static struct heap_block *heap_last_block(void) {
    struct heap_block *current = heap_first;

    while ((current != NULL) && (current->next != NULL)) {
        current = current->next;
    }

    return current;
}

static bool heap_grow_one_page(void) {
    struct heap_block *tail;
    struct heap_block *new_block;
    uintptr_t old_end;
    uint64_t physical_address;
    uint64_t translated;

    if (!heap_validate() || (heap_mapped_pages >= HEAP_MAX_PAGES) ||
        !heap_mapped_end(&old_end) ||
        (old_end > (HEAP_VIRTUAL_LIMIT - (uintptr_t)HEAP_PAGE_SIZE))) {
        return false;
    }

    tail = heap_last_block();
    if ((tail == NULL) ||
        ((tail->state == HEAP_STATE_FREE) &&
         (tail->size > (SIZE_MAX - HEAP_PAGE_SIZE)))) {
        return false;
    }

    if (vmm_translate(old_end, &translated) ||
        !pmm_alloc_frame(&physical_address)) {
        return false;
    }

    if (!vmm_map_page(old_end, physical_address, VMM_FLAG_WRITABLE)) {
        (void)pmm_free_frame(physical_address);
        return false;
    }

    heap_zero_page(old_end);
    ++heap_mapped_pages;

    if (tail->state == HEAP_STATE_FREE) {
        tail->size += HEAP_PAGE_SIZE;
        heap_refresh_block(tail);
    } else {
        new_block = (struct heap_block *)old_end;
        new_block->size = HEAP_PAGE_SIZE - sizeof(struct heap_block);
        new_block->prev = tail;
        new_block->next = NULL;
        new_block->state = HEAP_STATE_FREE;
        heap_refresh_block(new_block);

        tail->next = new_block;
        heap_refresh_block(tail);
    }

    if (!heap_validate()) {
        heap_corrupted = true;
        return false;
    }

    return true;
}

static bool heap_align_request(size_t size, size_t *aligned_size) {
    const size_t alignment = (size_t)KERNEL_HEAP_ALIGNMENT;

    if ((aligned_size == NULL) || (size == 0U) ||
        (size > (SIZE_MAX - (alignment - 1U)))) {
        return false;
    }

    *aligned_size = (size + (alignment - 1U)) & ~(alignment - 1U);
    return *aligned_size >= alignment;
}

static struct heap_block *heap_find_fit(size_t aligned_size) {
    struct heap_block *current = heap_first;

    while (current != NULL) {
        if ((current->state == HEAP_STATE_FREE) &&
            (current->size >= aligned_size)) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

static void *heap_allocate_from_block(struct heap_block *block,
                                      size_t aligned_size) {
    const size_t alignment = (size_t)KERNEL_HEAP_ALIGNMENT;
    const size_t header_size = sizeof(struct heap_block);
    size_t split_threshold = SIZE_MAX;
    uintptr_t payload_address;

    if (!heap_block_valid(block) ||
        (block->state != HEAP_STATE_FREE) ||
        (block->size < aligned_size)) {
        return NULL;
    }

    payload_address = (uintptr_t)block + header_size;

    if (aligned_size <= (SIZE_MAX - header_size - alignment)) {
        split_threshold = aligned_size + header_size + alignment;
    }

    if (block->size >= split_threshold) {
        struct heap_block *old_next = block->next;
        struct heap_block *new_block =
            (struct heap_block *)(payload_address + (uintptr_t)aligned_size);
        const size_t new_size = block->size - aligned_size - header_size;

        new_block->size = new_size;
        new_block->prev = block;
        new_block->next = old_next;
        new_block->state = HEAP_STATE_FREE;
        heap_refresh_block(new_block);

        if (old_next != NULL) {
            old_next->prev = new_block;
            heap_refresh_block(old_next);
        }

        block->size = aligned_size;
        block->next = new_block;
        block->state = HEAP_STATE_USED;
        heap_refresh_block(block);
    } else {
        block->state = HEAP_STATE_USED;
        heap_refresh_block(block);
    }

    if (!heap_validate()) {
        heap_corrupted = true;
        return NULL;
    }

    return (void *)payload_address;
}

static bool heap_merge_with_next(struct heap_block *left) {
    struct heap_block *right;
    struct heap_block *successor;
    size_t combined_size;

    if (!heap_block_valid(left) ||
        (left->state != HEAP_STATE_FREE) ||
        (left->next == NULL)) {
        return false;
    }

    right = left->next;
    if (!heap_block_valid(right) ||
        (right->state != HEAP_STATE_FREE) ||
        (right->prev != left) ||
        (left->size > (SIZE_MAX - sizeof(struct heap_block))) ||
        ((left->size + sizeof(struct heap_block)) >
         (SIZE_MAX - right->size))) {
        return false;
    }

    combined_size = left->size + sizeof(struct heap_block) + right->size;
    successor = right->next;

    left->size = combined_size;
    left->next = successor;
    if (successor != NULL) {
        successor->prev = left;
        heap_refresh_block(successor);
    }
    heap_refresh_block(left);

    right->magic = 0ULL;
    right->size = 0U;
    right->prev = NULL;
    right->next = NULL;
    right->state = 0ULL;
    right->guard = 0ULL;

    return true;
}

bool heap_init(void) {
    uint64_t frames[HEAP_INITIAL_PAGES];
    size_t mapped_count = 0U;
    size_t index;

    heap_reset_state();

    if (((HEAP_VIRTUAL_BASE & ((uintptr_t)HEAP_PAGE_SIZE - 1ULL)) != 0ULL) ||
        ((HEAP_VIRTUAL_LIMIT & ((uintptr_t)HEAP_PAGE_SIZE - 1ULL)) != 0ULL) ||
        (HEAP_VIRTUAL_LIMIT <= HEAP_VIRTUAL_BASE) ||
        (HEAP_INITIAL_PAGES > HEAP_MAX_PAGES) ||
        (HEAP_PAGE_SIZE <= sizeof(struct heap_block))) {
        return false;
    }

    for (index = 0U; index < (size_t)HEAP_INITIAL_PAGES; ++index) {
        uintptr_t virtual_address = HEAP_VIRTUAL_BASE +
            ((uintptr_t)index * (uintptr_t)HEAP_PAGE_SIZE);
        uint64_t physical_address;
        uint64_t translated;

        if (vmm_translate(virtual_address, &translated) ||
            !pmm_alloc_frame(&physical_address)) {
            heap_rollback_initial_pages(mapped_count, frames);
            return false;
        }

        if (!vmm_map_page(virtual_address, physical_address,
                          VMM_FLAG_WRITABLE)) {
            (void)pmm_free_frame(physical_address);
            heap_rollback_initial_pages(mapped_count, frames);
            return false;
        }

        frames[index] = physical_address;
        heap_zero_page(virtual_address);
        ++mapped_count;
    }

    heap_mapped_pages = mapped_count;
    heap_first = (struct heap_block *)HEAP_VIRTUAL_BASE;
    heap_first->size = (heap_mapped_pages * HEAP_PAGE_SIZE) -
                       sizeof(struct heap_block);
    heap_first->prev = NULL;
    heap_first->next = NULL;
    heap_first->state = HEAP_STATE_FREE;
    heap_refresh_block(heap_first);
    heap_initialized = true;

    if (!heap_validate()) {
        heap_corrupted = true;
        return false;
    }

    return true;
}

void *kmalloc(size_t size) {
    struct heap_block *block;
    size_t aligned_size;

    if ((!heap_initialized) || heap_corrupted ||
        !heap_align_request(size, &aligned_size) ||
        (aligned_size > (HEAP_REGION_SIZE - sizeof(struct heap_block)))) {
        return NULL;
    }

    if (!heap_validate()) {
        heap_corrupted = true;
        return NULL;
    }

    block = heap_find_fit(aligned_size);
    while (block == NULL) {
        if (!heap_grow_one_page()) {
            return NULL;
        }
        block = heap_find_fit(aligned_size);
    }

    return heap_allocate_from_block(block, aligned_size);
}

bool kfree(void *ptr) {
    struct heap_block *current;
    uintptr_t pointer_address;
    uintptr_t mapped_end;

    if ((!heap_initialized) || heap_corrupted || (ptr == NULL) ||
        !heap_mapped_end(&mapped_end)) {
        return false;
    }

    pointer_address = (uintptr_t)ptr;
    if ((pointer_address <
         (HEAP_VIRTUAL_BASE + sizeof(struct heap_block))) ||
        (pointer_address >= mapped_end) ||
        ((pointer_address & ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL)) {
        return false;
    }

    if (!heap_validate()) {
        heap_corrupted = true;
        return false;
    }

    current = heap_first;
    while (current != NULL) {
        const uintptr_t payload_address =
            (uintptr_t)current + sizeof(struct heap_block);

        if (payload_address == pointer_address) {
            if (current->state != HEAP_STATE_USED) {
                return false;
            }

            current->state = HEAP_STATE_FREE;
            heap_refresh_block(current);

            if ((current->next != NULL) &&
                (current->next->state == HEAP_STATE_FREE) &&
                !heap_merge_with_next(current)) {
                heap_corrupted = true;
                return false;
            }

            if ((current->prev != NULL) &&
                (current->prev->state == HEAP_STATE_FREE)) {
                current = current->prev;
                if (!heap_merge_with_next(current)) {
                    heap_corrupted = true;
                    return false;
                }
            }

            if (!heap_validate()) {
                heap_corrupted = true;
                return false;
            }

            return true;
        }

        current = current->next;
    }

    return false;
}

bool heap_get_stats(struct heap_stats *stats) {
    struct heap_block *current;
    uint64_t used_bytes = 0ULL;
    uint64_t free_bytes = 0ULL;
    uint64_t allocation_count = 0ULL;

    if ((!heap_initialized) || heap_corrupted || (stats == NULL)) {
        return false;
    }

    if (!heap_validate()) {
        heap_corrupted = true;
        return false;
    }

    current = heap_first;
    while (current != NULL) {
        if (current->state == HEAP_STATE_USED) {
            used_bytes += (uint64_t)current->size;
            ++allocation_count;
        } else {
            free_bytes += (uint64_t)current->size;
        }
        current = current->next;
    }

    stats->virtual_base = HEAP_VIRTUAL_BASE;
    stats->virtual_limit = HEAP_VIRTUAL_LIMIT;
    stats->mapped_pages = (uint64_t)heap_mapped_pages;
    stats->mapped_bytes = (uint64_t)(heap_mapped_pages * HEAP_PAGE_SIZE);
    stats->used_bytes = used_bytes;
    stats->free_bytes = free_bytes;
    stats->allocation_count = allocation_count;
    return true;
}
