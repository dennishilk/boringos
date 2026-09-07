#include <stddef.h>
#include <stdint.h>

#include <boring/memory.h>
#include <boring/runtime.h>
#include <boring/runtime_smoke.h>
#include <boring/string.h>
#include <boring/syscall.h>

static const char runtime_message[] = "hello from BoringOS C userspace";
static const uint8_t runtime_copy_source[8] = {
    0x42U, 0x4fU, 0x52U, 0x49U, 0x4eU, 0x47U, 0x4fU, 0x53U
};

volatile struct boring_runtime_smoke_result boring_runtime_smoke_result
    __attribute__((section(".runtime_result"), aligned(16))) = { 0ULL };

volatile uint64_t boring_runtime_data_marker =
    BORING_RUNTIME_SMOKE_DATA_MARKER;

volatile uint64_t boring_runtime_bss_probe
    __attribute__((section(".bss.runtime_probe")));

static uint16_t runtime_read_cs(void) {
    uint16_t selector;

    __asm__ volatile("movw %%cs, %0" : "=r"(selector));
    return selector;
}

static int bytes_equal(const uint8_t *first,
                       const uint8_t *second,
                       size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        if (first[index] != second[index]) {
            return 0;
        }
    }
    return 1;
}

int boring_main(void) {
    uint8_t local_buffer[16];
    uint8_t copy_buffer[8];
    volatile uint64_t local_guard = BORING_RUNTIME_SMOKE_LOCAL_MARKER;
    uint8_t expected_fill[16];
    uint64_t pid;
    long debug_result;
    size_t index;

    boring_runtime_smoke_result.cs = (uint64_t)runtime_read_cs();
    boring_runtime_smoke_result.entered = 1ULL;

    if (boring_runtime_data_marker != BORING_RUNTIME_SMOKE_DATA_MARKER) {
        return 101;
    }
    boring_runtime_smoke_result.data_ok = 1ULL;

    if (boring_runtime_bss_probe != 0ULL) {
        return 102;
    }
    boring_runtime_smoke_result.bss_zero = 1ULL;
    boring_runtime_bss_probe = BORING_RUNTIME_SMOKE_BSS_MARKER;

    for (index = 0U; index < sizeof(expected_fill); ++index) {
        expected_fill[index] = 0x5aU;
    }
    if (local_guard != BORING_RUNTIME_SMOKE_LOCAL_MARKER) {
        return 103;
    }
    boring_runtime_smoke_result.local_stack_ok = 1ULL;

    if (boring_strlen(runtime_message) !=
        (size_t)BORING_RUNTIME_SMOKE_MESSAGE_LENGTH) {
        return 104;
    }
    boring_runtime_smoke_result.strlen_ok = 1ULL;

    if ((boring_memset(local_buffer, 0x5a, sizeof(local_buffer)) !=
         (void *)local_buffer) ||
        !bytes_equal(local_buffer, expected_fill, sizeof(local_buffer))) {
        return 105;
    }
    boring_runtime_smoke_result.memset_ok = 1ULL;

    if ((boring_memcpy(copy_buffer, runtime_copy_source,
                       sizeof(copy_buffer)) != (void *)copy_buffer) ||
        !bytes_equal(copy_buffer, runtime_copy_source, sizeof(copy_buffer))) {
        return 106;
    }
    boring_runtime_smoke_result.memcpy_ok = 1ULL;

    pid = boring_getpid();
    boring_runtime_smoke_result.getpid_result = pid;
    if (pid != 1ULL) {
        return 107;
    }
    boring_runtime_smoke_result.getpid_ok = 1ULL;

    debug_result = boring_debug_write(runtime_message,
                                      (size_t)BORING_RUNTIME_SMOKE_MESSAGE_LENGTH);
    boring_runtime_smoke_result.debug_result = (uint64_t)debug_result;
    if (debug_result != (long)BORING_RUNTIME_SMOKE_MESSAGE_LENGTH) {
        return 108;
    }
    boring_runtime_smoke_result.debug_ok = 1ULL;
    boring_runtime_smoke_result.sysret_resume = 1ULL;

    if (local_guard != BORING_RUNTIME_SMOKE_LOCAL_MARKER) {
        return 109;
    }
    boring_runtime_smoke_result.ready_to_return = 1ULL;

    return BORING_RUNTIME_SMOKE_MAIN_RETURN;
}
