#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/memory.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define MEMORY_TEST_DIRECT_PAGES 8U
#define MEMORY_TEST_BUFFER_PAGES 3U
#define MEMORY_TEST_LINE_CAPACITY 192U

int boring_main(int argc, char **argv);

static bool write_all(const char *text, size_t length) {
    size_t offset = 0U;

    while (offset < length) {
        const long result = boring_fd_write(BORING_FD_STDOUT,
                                            &text[offset], length - offset);
        if ((result <= 0L) || ((size_t)result > length - offset)) {
            return false;
        }
        offset += (size_t)result;
    }
    return true;
}

static bool write_text(const char *text) {
    return (text != NULL) && write_all(text, boring_strlen(text));
}

static bool literal_equals(const char *text, const char *literal) {
    size_t index = 0U;

    if ((text == NULL) || (literal == NULL)) {
        return false;
    }
    while ((text[index] != '\0') && (literal[index] != '\0')) {
        if (text[index] != literal[index]) {
            return false;
        }
        ++index;
    }
    return text[index] == literal[index];
}

static bool append_text(char *buffer, size_t capacity, size_t *length,
                        const char *text) {
    size_t index;

    if ((buffer == NULL) || (length == NULL) || (text == NULL)) {
        return false;
    }
    for (index = 0U; text[index] != '\0'; ++index) {
        if (*length >= capacity) {
            return false;
        }
        buffer[*length] = text[index];
        ++(*length);
    }
    return true;
}

static bool append_u64(char *buffer, size_t capacity, size_t *length,
                       uint64_t value) {
    char digits[20];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (char)(value % 10ULL));
        ++count;
        value /= 10ULL;
    } while (value != 0ULL);
    while (count != 0U) {
        --count;
        if (*length >= capacity) {
            return false;
        }
        buffer[*length] = digits[count];
        ++(*length);
    }
    return true;
}

static bool print_accounting(uint64_t before, uint64_t during, uint64_t after) {
    char line[MEMORY_TEST_LINE_CAPACITY];
    size_t length = 0U;

    if (!append_text(line, sizeof(line), &length, "memory-test: pmm before=") ||
        !append_u64(line, sizeof(line), &length, before) ||
        !append_text(line, sizeof(line), &length, " during=") ||
        !append_u64(line, sizeof(line), &length, during) ||
        !append_text(line, sizeof(line), &length, " after=") ||
        !append_u64(line, sizeof(line), &length, after) ||
        !append_text(line, sizeof(line), &length, "\n")) {
        return false;
    }
    return write_all(line, length);
}

static bool expect_error(long result, int error_number) {
    return result == -(long)error_number;
}

static bool bytes_zero(const uint8_t *bytes, size_t length) {
    size_t index;

    if (bytes == NULL) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool anonymous_acceptance(void) {
    struct boring_system_info before;
    struct boring_system_info during;
    struct boring_system_info after;
    long raw;
    uint8_t *memory;
    uint8_t *reused;
    size_t index;

    if (boring_system_info(&before) != 0L) {
        return false;
    }
    raw = boring_memory_alloc_raw(
        (size_t)MEMORY_TEST_DIRECT_PAGES * (size_t)BORING_MEMORY_PAGE_SIZE);
    if (raw <= 0L) {
        return false;
    }
    memory = (uint8_t *)(uintptr_t)raw;
    if ((((uintptr_t)memory & (uintptr_t)(BORING_MEMORY_PAGE_SIZE - 1ULL)) != 0U) ||
        !bytes_zero(memory, (size_t)MEMORY_TEST_DIRECT_PAGES *
                            (size_t)BORING_MEMORY_PAGE_SIZE)) {
        return false;
    }
    for (index = 0U;
         index < (size_t)MEMORY_TEST_DIRECT_PAGES *
                 (size_t)BORING_MEMORY_PAGE_SIZE; ++index) {
        memory[index] = (uint8_t)((index * 17U + 3U) & 0xffU);
    }
    for (index = 0U;
         index < (size_t)MEMORY_TEST_DIRECT_PAGES *
                 (size_t)BORING_MEMORY_PAGE_SIZE; ++index) {
        if (memory[index] != (uint8_t)((index * 17U + 3U) & 0xffU)) {
            return false;
        }
    }
    if ((boring_system_info(&during) != 0L) ||
        (during.free_memory_bytes >= before.free_memory_bytes) ||
        (boring_memory_free(memory) != 0L) ||
        (boring_system_info(&after) != 0L) ||
        (after.free_memory_bytes < during.free_memory_bytes) ||
        !print_accounting(before.free_memory_bytes, during.free_memory_bytes,
                          after.free_memory_bytes)) {
        return false;
    }
    if (!write_text("memory-test: anonymous rw-nx allocation passed\n")) {
        return false;
    }

    reused = (uint8_t *)boring_memory_alloc((size_t)BORING_MEMORY_PAGE_SIZE);
    if ((reused == NULL) || !bytes_zero(reused, (size_t)BORING_MEMORY_PAGE_SIZE)) {
        return false;
    }
    for (index = 0U; index < (size_t)BORING_MEMORY_PAGE_SIZE; ++index) {
        reused[index] = 0xd7U;
    }
    if (boring_memory_free(reused) != 0L) {
        return false;
    }
    reused = (uint8_t *)boring_memory_alloc((size_t)BORING_MEMORY_PAGE_SIZE);
    if ((reused == NULL) || !bytes_zero(reused, (size_t)BORING_MEMORY_PAGE_SIZE) ||
        (boring_memory_free(reused) != 0L)) {
        return false;
    }
    return write_text("memory-test: anonymous zero reuse passed\n");
}

static bool anonymous_negatives(void) {
    void *allocations[BORING_MEMORY_ALLOCATION_MAX];
    long raw;
    uint8_t *two_pages;
    size_t index;

    if (!expect_error(boring_memory_alloc_raw(0U), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_memory_alloc_raw(
                          (size_t)BORING_MEMORY_ALLOC_MAX_BYTES + 1U),
                      BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_memory_alloc_raw(SIZE_MAX), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_memory_free(NULL), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_memory_free((void *)(uintptr_t)0x100000000ULL),
                      BORING_SYSCALL_EINVAL)) {
        return false;
    }

    two_pages = (uint8_t *)boring_memory_alloc(
        2U * (size_t)BORING_MEMORY_PAGE_SIZE);
    if ((two_pages == NULL) ||
        !expect_error(boring_memory_free(two_pages + BORING_MEMORY_PAGE_SIZE),
                      BORING_SYSCALL_EINVAL) ||
        (boring_memory_free(two_pages) != 0L) ||
        !expect_error(boring_memory_free(two_pages), BORING_SYSCALL_EINVAL)) {
        return false;
    }

    for (index = 0U; index < (size_t)BORING_MEMORY_ALLOCATION_MAX; ++index) {
        allocations[index] = boring_memory_alloc(1U);
        if (allocations[index] == NULL) {
            return false;
        }
    }
    raw = boring_memory_alloc_raw(1U);
    if (!expect_error(raw, BORING_SYSCALL_ENOSPC)) {
        return false;
    }
    for (index = 0U; index < (size_t)BORING_MEMORY_ALLOCATION_MAX; ++index) {
        if (boring_memory_free(allocations[index]) != 0L) {
            return false;
        }
    }
    return true;
}

static bool heap_acceptance(void) {
    uint8_t *one;
    uint8_t *sixteen;
    uint8_t *seventeen;
    uint8_t *medium;
    uint8_t *multi_page;
    uint8_t *zeroed;
    uint8_t *reused;
    size_t index;

    one = (uint8_t *)boring_malloc(1U);
    sixteen = (uint8_t *)boring_malloc(16U);
    seventeen = (uint8_t *)boring_malloc(17U);
    medium = (uint8_t *)boring_malloc(2048U);
    multi_page = (uint8_t *)boring_malloc(32768U);
    if ((one == NULL) || (sixteen == NULL) || (seventeen == NULL) ||
        (medium == NULL) || (multi_page == NULL) ||
        (((uintptr_t)one & 15U) != 0U) ||
        (((uintptr_t)sixteen & 15U) != 0U) ||
        (((uintptr_t)seventeen & 15U) != 0U) ||
        (((uintptr_t)medium & 15U) != 0U) ||
        (((uintptr_t)multi_page & 15U) != 0U)) {
        return false;
    }
    one[0] = 0x42U;
    for (index = 0U; index < 32768U; ++index) {
        multi_page[index] = (uint8_t)(index & 0xffU);
    }
    if ((one[0] != 0x42U) || (multi_page[32767] != 0xffU)) {
        return false;
    }
    boring_free(sixteen);
    reused = (uint8_t *)boring_malloc(16U);
    if (reused != sixteen) {
        return false;
    }
    boring_free(reused);
    boring_free(reused);
    boring_free(NULL);

    zeroed = (uint8_t *)boring_calloc(257U, 7U);
    if ((zeroed == NULL) || !bytes_zero(zeroed, 257U * 7U) ||
        (boring_calloc(SIZE_MAX, 2U) != NULL) ||
        (boring_malloc(0U) != NULL)) {
        return false;
    }
    boring_free(one);
    boring_free(seventeen);
    boring_free(medium);
    boring_free(multi_page);
    boring_free(zeroed);
    return write_text("memory-test: heap allocator passed\n");
}

static bool shared_buffer_acceptance(void) {
    long handle_result;
    uint32_t handle;
    uint8_t *alias_a;
    uint8_t *alias_b;
    uint8_t *anonymous;
    uint8_t *recreated;
    long recreated_handle_result;
    uint32_t recreated_handle;
    size_t index;

    handle_result = boring_buffer_create(
        (size_t)MEMORY_TEST_BUFFER_PAGES * (size_t)BORING_MEMORY_PAGE_SIZE);
    if (handle_result <= 0L) {
        return false;
    }
    handle = (uint32_t)handle_result;
    alias_a = (uint8_t *)boring_buffer_map(handle);
    alias_b = (uint8_t *)boring_buffer_map(handle);
    if ((alias_a == NULL) || (alias_b == NULL) || (alias_a == alias_b) ||
        !bytes_zero(alias_a, (size_t)MEMORY_TEST_BUFFER_PAGES *
                             (size_t)BORING_MEMORY_PAGE_SIZE)) {
        return false;
    }
    alias_a[123] = 0x5aU;
    alias_a[BORING_MEMORY_PAGE_SIZE + 17U] = 0xc3U;
    alias_a[(MEMORY_TEST_BUFFER_PAGES * BORING_MEMORY_PAGE_SIZE) - 1U] = 0x7eU;
    if ((alias_b[123] != 0x5aU) ||
        (alias_b[BORING_MEMORY_PAGE_SIZE + 17U] != 0xc3U) ||
        (alias_b[(MEMORY_TEST_BUFFER_PAGES * BORING_MEMORY_PAGE_SIZE) - 1U] !=
         0x7eU)) {
        return false;
    }
    if ((boring_buffer_unmap(alias_a) != 0L) || (alias_b[123] != 0x5aU) ||
        (boring_buffer_close(handle) != 0L) || (alias_b[123] != 0x5aU) ||
        !expect_error(boring_buffer_map_raw(handle), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_close(handle), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_unmap(alias_b + 1U), BORING_SYSCALL_EINVAL)) {
        return false;
    }

    anonymous = (uint8_t *)boring_memory_alloc((size_t)BORING_MEMORY_PAGE_SIZE);
    if ((anonymous == NULL) ||
        !expect_error(boring_buffer_unmap(anonymous), BORING_SYSCALL_EINVAL) ||
        (boring_memory_free(anonymous) != 0L) ||
        (boring_buffer_unmap(alias_b) != 0L)) {
        return false;
    }
    if (!write_text("memory-test: shared dual-alias proof passed\n") ||
        !write_text("memory-test: close keeps mapping alive passed\n")) {
        return false;
    }

    recreated_handle_result = boring_buffer_create((size_t)BORING_MEMORY_PAGE_SIZE);
    if (recreated_handle_result <= 0L) {
        return false;
    }
    recreated_handle = (uint32_t)recreated_handle_result;
    recreated = (uint8_t *)boring_buffer_map(recreated_handle);
    if ((recreated == NULL) ||
        !bytes_zero(recreated, (size_t)BORING_MEMORY_PAGE_SIZE)) {
        return false;
    }
    for (index = 0U; index < (size_t)BORING_MEMORY_PAGE_SIZE; ++index) {
        recreated[index] = 0x91U;
    }
    if ((boring_buffer_unmap(recreated) != 0L) ||
        (boring_buffer_close(recreated_handle) != 0L)) {
        return false;
    }

    recreated_handle_result = boring_buffer_create((size_t)BORING_MEMORY_PAGE_SIZE);
    if (recreated_handle_result <= 0L) {
        return false;
    }
    recreated_handle = (uint32_t)recreated_handle_result;
    recreated = (uint8_t *)boring_buffer_map(recreated_handle);
    if ((recreated == NULL) ||
        !bytes_zero(recreated, (size_t)BORING_MEMORY_PAGE_SIZE) ||
        (boring_buffer_unmap(recreated) != 0L) ||
        (boring_buffer_close(recreated_handle) != 0L)) {
        return false;
    }

    if (!expect_error(boring_buffer_create(0U), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_create(
                          (size_t)BORING_BUFFER_MAX_BYTES + 1U),
                      BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_create(SIZE_MAX), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_map_raw(BORING_BUFFER_HANDLE_INVALID),
                      BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_unmap(NULL), BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_unmap((void *)(uintptr_t)0x100000000ULL),
                      BORING_SYSCALL_EINVAL) ||
        !expect_error(boring_buffer_close(BORING_BUFFER_HANDLE_INVALID),
                      BORING_SYSCALL_EINVAL)) {
        return false;
    }
    return true;
}

static void teardown_mode(void) {
    void *anonymous;
    long handle_result;
    void *mapping;

    anonymous = boring_memory_alloc(2U * (size_t)BORING_MEMORY_PAGE_SIZE);
    handle_result = boring_buffer_create(2U * (size_t)BORING_MEMORY_PAGE_SIZE);
    if ((anonymous == NULL) || (handle_result <= 0L)) {
        (void)write_text("memory-test: teardown setup failed\n");
        boring_exit(1);
    }
    mapping = boring_buffer_map((uint32_t)handle_result);
    if (mapping == NULL) {
        (void)write_text("memory-test: teardown map failed\n");
        boring_exit(1);
    }
    ((volatile uint8_t *)anonymous)[0] = 0xa1U;
    ((volatile uint8_t *)mapping)[0] = 0xb2U;
    (void)write_text("memory-test: leaving resources for exit cleanup\n");
    boring_exit(0);
}

int boring_main(int argc, char **argv) {
    const bool teardown = (argc == 2) && (argv != NULL) &&
                          literal_equals(argv[1], "--teardown");

    if (teardown) {
        teardown_mode();
    }
    if ((argc != 1) || (argv == NULL) ||
        !write_text("memory-test: start\n") ||
        !anonymous_acceptance() ||
        !anonymous_negatives() ||
        !heap_acceptance() ||
        !shared_buffer_acceptance() ||
        !write_text("memory-test: syscall negatives passed\n") ||
        !write_text("memory-test: explicit cleanup passed\n") ||
        !write_text("memory-test: witness complete\n")) {
        (void)write_text("memory-test: FAILED\n");
        boring_exit(1);
    }
    boring_exit(0);
}
