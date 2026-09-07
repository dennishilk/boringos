#ifndef BORING_RUNTIME_SMOKE_H
#define BORING_RUNTIME_SMOKE_H

#define BORING_RUNTIME_SMOKE_TEXT_VA 0x0000000040000000ULL
#define BORING_RUNTIME_SMOKE_RODATA_VA 0x0000000040001000ULL
#define BORING_RUNTIME_SMOKE_DATA_VA 0x0000000040002000ULL
#define BORING_RUNTIME_SMOKE_BSS_VA 0x0000000040002100ULL
#define BORING_RUNTIME_SMOKE_STACK_BASE 0x0000000040010000ULL
#define BORING_RUNTIME_SMOKE_STACK_TOP 0x0000000040011000ULL

#define BORING_RUNTIME_SMOKE_MESSAGE_LENGTH 31U
#define BORING_RUNTIME_SMOKE_MAIN_RETURN 42
#define BORING_RUNTIME_SMOKE_DATA_MARKER 0x52554e54494d4531ULL
#define BORING_RUNTIME_SMOKE_BSS_MARKER 0x42535352554e544dULL
#define BORING_RUNTIME_SMOKE_LOCAL_MARKER 0x535441434b4c4f43ULL

#ifndef __ASSEMBLER__
#include <stdint.h>

struct boring_runtime_smoke_result {
    uint64_t cs;
    uint64_t entered;
    uint64_t data_ok;
    uint64_t bss_zero;
    uint64_t local_stack_ok;
    uint64_t strlen_ok;
    uint64_t memset_ok;
    uint64_t memcpy_ok;
    uint64_t getpid_result;
    uint64_t getpid_ok;
    uint64_t debug_result;
    uint64_t debug_ok;
    uint64_t sysret_resume;
    uint64_t ready_to_return;
};
#endif

#endif
