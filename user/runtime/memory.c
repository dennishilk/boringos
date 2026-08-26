#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/memory.h>
#include <boring/syscall.h>

#define BORING_HEAP_ALIGNMENT 16U
#define BORING_HEAP_PAGE_SIZE 4096U
#define BORING_HEAP_ARENA_MIN (16U * 1024U)
#define BORING_HEAP_BLOCK_MAGIC 0x424f52494e47484dULL
#define BORING_HEAP_BLOCK_FREE 0U
#define BORING_HEAP_BLOCK_USED 1U

struct boring_heap_block {
    uint64_t magic;
    size_t size;
    struct boring_heap_block *next;
    struct boring_heap_block *previous;
    uintptr_t arena_base;
    size_t arena_size;
    uint32_t state;
    uint32_t reserved;
    uint64_t padding;
};

_Static_assert(sizeof(struct boring_heap_block) == 64U,
               "userspace heap block header must remain 16-byte aligned");

static struct boring_heap_block *heap_first;
static struct boring_heap_block *heap_last;

void *boring_memcpy(void *destination, const void *source, size_t length) {
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;
    size_t index;

    for (index = 0U; index < length; ++index) {
        output[index] = input[index];
    }

    return destination;
}

void *boring_memset(void *destination, int value, size_t length) {
    uint8_t *output = (uint8_t *)destination;
    const uint8_t byte = (uint8_t)value;
    size_t index;

    for (index = 0U; index < length; ++index) {
        output[index] = byte;
    }

    return destination;
}

static bool heap_align_size(size_t size, size_t *aligned) {
    const size_t mask = (size_t)BORING_HEAP_ALIGNMENT - 1U;

    if ((aligned == NULL) || (size == 0U) || (size > SIZE_MAX - mask)) {
        return false;
    }
    *aligned = (size + mask) & ~mask;
    return *aligned != 0U;
}

static bool heap_round_arena(size_t payload, size_t *arena_size) {
    size_t required;
    const size_t page_mask = (size_t)BORING_HEAP_PAGE_SIZE - 1U;

    if ((arena_size == NULL) ||
        (payload > SIZE_MAX - sizeof(struct boring_heap_block))) {
        return false;
    }
    required = payload + sizeof(struct boring_heap_block);
    if (required < (size_t)BORING_HEAP_ARENA_MIN) {
        required = (size_t)BORING_HEAP_ARENA_MIN;
    }
    if (required > SIZE_MAX - page_mask) {
        return false;
    }
    *arena_size = (required + page_mask) & ~page_mask;
    return *arena_size >= required;
}

static uint8_t *heap_payload(struct boring_heap_block *block) {
    return (uint8_t *)(void *)(block + 1);
}

static bool heap_block_valid(const struct boring_heap_block *block) {
    return (block != NULL) && (block->magic == BORING_HEAP_BLOCK_MAGIC) &&
           ((block->state == BORING_HEAP_BLOCK_FREE) ||
            (block->state == BORING_HEAP_BLOCK_USED)) &&
           (((uintptr_t)(const void *)(block + 1) &
             ((uintptr_t)BORING_HEAP_ALIGNMENT - 1U)) == 0U);
}

static bool heap_same_arena(const struct boring_heap_block *first,
                            const struct boring_heap_block *second) {
    return heap_block_valid(first) && heap_block_valid(second) &&
           (first->arena_base == second->arena_base) &&
           (first->arena_size == second->arena_size);
}

static bool heap_physically_adjacent(const struct boring_heap_block *first,
                                     const struct boring_heap_block *second) {
    uintptr_t payload;
    uintptr_t first_end;

    if (!heap_same_arena(first, second)) {
        return false;
    }
    payload = (uintptr_t)(const void *)(first + 1);
    if ((uintptr_t)first->size > UINTPTR_MAX - payload) {
        return false;
    }
    first_end = payload + (uintptr_t)first->size;
    return first_end == (uintptr_t)(const void *)second;
}

static bool heap_split(struct boring_heap_block *block, size_t requested) {
    struct boring_heap_block *split;
    const size_t minimum_tail =
        sizeof(struct boring_heap_block) + (size_t)BORING_HEAP_ALIGNMENT;

    if (!heap_block_valid(block) || (block->state != BORING_HEAP_BLOCK_FREE) ||
        (block->size < requested)) {
        return false;
    }
    if ((block->size - requested) < minimum_tail) {
        return true;
    }

    split = (struct boring_heap_block *)(void *)(heap_payload(block) + requested);
    split->magic = BORING_HEAP_BLOCK_MAGIC;
    split->size = block->size - requested - sizeof(*split);
    split->next = block->next;
    split->previous = block;
    split->arena_base = block->arena_base;
    split->arena_size = block->arena_size;
    split->state = BORING_HEAP_BLOCK_FREE;
    split->reserved = 0U;
    split->padding = 0ULL;
    if (block->next != NULL) {
        block->next->previous = split;
    } else {
        heap_last = split;
    }
    block->next = split;
    block->size = requested;
    return true;
}

static bool heap_grow(size_t requested, struct boring_heap_block **block_out) {
    size_t arena_size;
    void *arena;
    struct boring_heap_block *block;

    if ((block_out == NULL) || !heap_round_arena(requested, &arena_size)) {
        return false;
    }
    arena = boring_memory_alloc(arena_size);
    if (arena == NULL) {
        return false;
    }
    block = (struct boring_heap_block *)arena;
    block->magic = BORING_HEAP_BLOCK_MAGIC;
    block->size = arena_size - sizeof(*block);
    block->next = NULL;
    block->previous = heap_last;
    block->arena_base = (uintptr_t)arena;
    block->arena_size = arena_size;
    block->state = BORING_HEAP_BLOCK_FREE;
    block->reserved = 0U;
    block->padding = 0ULL;
    if (heap_last != NULL) {
        heap_last->next = block;
    } else {
        heap_first = block;
    }
    heap_last = block;
    *block_out = block;
    return true;
}

void *boring_malloc(size_t size) {
    struct boring_heap_block *block;
    size_t requested;

    if (!heap_align_size(size, &requested)) {
        return NULL;
    }
    for (block = heap_first; block != NULL; block = block->next) {
        if (!heap_block_valid(block)) {
            return NULL;
        }
        if ((block->state == BORING_HEAP_BLOCK_FREE) &&
            (block->size >= requested)) {
            if (!heap_split(block, requested)) {
                return NULL;
            }
            block->state = BORING_HEAP_BLOCK_USED;
            return heap_payload(block);
        }
    }
    if (!heap_grow(requested, &block) || !heap_split(block, requested)) {
        return NULL;
    }
    block->state = BORING_HEAP_BLOCK_USED;
    return heap_payload(block);
}

void *boring_calloc(size_t count, size_t size) {
    size_t total;
    void *pointer;

    if ((count == 0U) || (size == 0U) || (size > SIZE_MAX / count)) {
        return NULL;
    }
    total = count * size;
    pointer = boring_malloc(total);
    if (pointer != NULL) {
        (void)boring_memset(pointer, 0, total);
    }
    return pointer;
}

static void heap_merge_next(struct boring_heap_block *block) {
    struct boring_heap_block *next;

    if (!heap_block_valid(block) || (block->state != BORING_HEAP_BLOCK_FREE)) {
        return;
    }
    next = block->next;
    if (!heap_block_valid(next) || (next->state != BORING_HEAP_BLOCK_FREE) ||
        !heap_physically_adjacent(block, next)) {
        return;
    }
    block->size += sizeof(*next) + next->size;
    block->next = next->next;
    if (next->next != NULL) {
        next->next->previous = block;
    } else {
        heap_last = block;
    }
    next->magic = 0ULL;
    next->size = 0U;
    next->next = NULL;
    next->previous = NULL;
    next->arena_base = 0U;
    next->arena_size = 0U;
    next->state = BORING_HEAP_BLOCK_FREE;
    next->reserved = 0U;
    next->padding = 0ULL;
}

void boring_free(void *pointer) {
    struct boring_heap_block *block;

    if (pointer == NULL) {
        return;
    }
    for (block = heap_first; block != NULL; block = block->next) {
        if (!heap_block_valid(block)) {
            return;
        }
        if ((void *)heap_payload(block) != pointer) {
            continue;
        }
        if (block->state == BORING_HEAP_BLOCK_FREE) {
            return;
        }
        block->state = BORING_HEAP_BLOCK_FREE;
        heap_merge_next(block);
        if ((block->previous != NULL) &&
            (block->previous->state == BORING_HEAP_BLOCK_FREE) &&
            heap_physically_adjacent(block->previous, block)) {
            block = block->previous;
            heap_merge_next(block);
        }
        return;
    }
}
