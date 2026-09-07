#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <boring/memory.h>
#include <boring/syscall.h>

#define HOST_HEAP_POOL_SIZE (512U * 1024U)
#define HOST_PAGE_SIZE 4096U

static uint8_t host_heap_pool[HOST_HEAP_POOL_SIZE]
    __attribute__((aligned(HOST_PAGE_SIZE)));
static size_t host_heap_offset;
static size_t memory_alloc_calls;

static void fail(const char *message) {
    (void)fprintf(stderr, "runtime-heap-host-test: FAIL: %s\n", message);
    exit(1);
}

static void require(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

void *boring_memory_alloc(size_t size) {
    size_t index;

    if ((size == 0U) || ((size & (HOST_PAGE_SIZE - 1U)) != 0U) ||
        (host_heap_offset > HOST_HEAP_POOL_SIZE) ||
        (size > HOST_HEAP_POOL_SIZE - host_heap_offset)) {
        return NULL;
    }
    for (index = 0U; index < size; ++index) {
        host_heap_pool[host_heap_offset + index] = 0U;
    }
    {
        void *const result = &host_heap_pool[host_heap_offset];
        host_heap_offset += size;
        ++memory_alloc_calls;
        return result;
    }
}

static bool aligned_16(const void *pointer) {
    return (pointer != NULL) &&
           (((uintptr_t)pointer & (uintptr_t)15U) == 0U);
}

int main(void) {
    uint8_t *one;
    uint8_t *sixteen;
    uint8_t *seventeen;
    uint8_t *first;
    uint8_t *second;
    uint8_t *third;
    uint8_t *coalesced;
    uint8_t *reused;
    uint8_t *medium;
    uint8_t *large;
    uint8_t *zeroed;
    size_t index;

    require(boring_malloc(0U) == NULL, "malloc(0) returns NULL");
    require(boring_calloc(0U, 16U) == NULL, "calloc zero count returns NULL");
    require(boring_calloc(16U, 0U) == NULL, "calloc zero size returns NULL");

    one = (uint8_t *)boring_malloc(1U);
    sixteen = (uint8_t *)boring_malloc(16U);
    seventeen = (uint8_t *)boring_malloc(17U);
    require(aligned_16(one) && aligned_16(sixteen) && aligned_16(seventeen),
            "malloc results are 16-byte aligned");
    one[0] = 0x11U;
    for (index = 0U; index < 16U; ++index) {
        sixteen[index] = (uint8_t)(0x20U + index);
    }
    for (index = 0U; index < 17U; ++index) {
        seventeen[index] = (uint8_t)(0x40U + index);
    }
    require(one[0] == 0x11U && sixteen[15] == 0x2fU &&
            seventeen[16] == 0x50U,
            "small allocation write/read patterns survive");

    first = (uint8_t *)boring_malloc(2048U);
    second = (uint8_t *)boring_malloc(2048U);
    third = (uint8_t *)boring_malloc(2048U);
    require((first != NULL) && (second != NULL) && (third != NULL),
            "split blocks allocate");
    boring_free(first);
    boring_free(second);
    coalesced = (uint8_t *)boring_malloc(4000U);
    require(coalesced == first, "adjacent free blocks coalesce for first-fit");

    boring_free(sixteen);
    reused = (uint8_t *)boring_malloc(16U);
    require(reused == sixteen, "freed block reused deterministically");
    boring_free(reused);
    boring_free(reused);
    boring_free(NULL);

    medium = (uint8_t *)boring_malloc(3000U);
    require(aligned_16(medium), "medium allocation succeeds");
    large = (uint8_t *)boring_malloc(32768U);
    require(aligned_16(large), "multi-page allocation succeeds");
    require(memory_alloc_calls >= 2U, "heap grows into another MEMORY_ALLOC arena");
    for (index = 0U; index < 32768U; ++index) {
        large[index] = (uint8_t)(index & 0xffU);
    }
    require(large[0] == 0U && large[255] == 0xffU &&
            large[32767] == 0xffU,
            "multi-page allocation write/read pattern survives");

    zeroed = (uint8_t *)boring_calloc(257U, 7U);
    require(zeroed != NULL, "calloc allocation succeeds");
    for (index = 0U; index < 257U * 7U; ++index) {
        require(zeroed[index] == 0U, "calloc payload is zero");
    }
    require(boring_calloc(SIZE_MAX, 2U) == NULL,
            "calloc multiplication overflow rejected");

    boring_free(one);
    boring_free(seventeen);
    boring_free(third);
    boring_free(coalesced);
    boring_free(medium);
    boring_free(large);
    boring_free(zeroed);

    (void)puts("M32 native userspace heap host tests passed.");
    return 0;
}
