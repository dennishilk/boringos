#ifndef BORING_SPAWN_STACK_H
#define BORING_SPAWN_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <boring/elf_loader.h>
#include <boring/syscall_abi.h>

#define BORING_SPAWN_STACK_BASE ((uintptr_t)0x40010000ULL)
/* One maximum FD I/O buffer plus bounded call/entry headroom. */
#define BORING_SPAWN_STACK_RESERVE ((size_t)BORING_SYSCALL_FD_IO_MAX + 2048U)
#define BORING_SPAWN_ARG_POINTER_BYTES \
    (((size_t)BORING_SYSCALL_ARG_MAX + 1U) * sizeof(uint64_t))
#define BORING_SPAWN_ARG_BLOCK_MAX \
    (BORING_SPAWN_ARG_POINTER_BYTES + (size_t)BORING_SYSCALL_ARG_BYTES_MAX + 15U)
#define BORING_SPAWN_STACK_PAGES \
    ((BORING_SPAWN_STACK_RESERVE + BORING_SPAWN_ARG_BLOCK_MAX + \
      (size_t)BORING_ELF_PAGE_SIZE - 1U) / (size_t)BORING_ELF_PAGE_SIZE)
#define BORING_SPAWN_STACK_SIZE \
    (BORING_SPAWN_STACK_PAGES * (size_t)BORING_ELF_PAGE_SIZE)

bool boring_spawn_stack_layout(uintptr_t base, uintptr_t top,
                                size_t argc, size_t argument_bytes,
                                uintptr_t *rsp, size_t *block_size);

#endif
