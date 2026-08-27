#include <boring/spawn_stack.h>

_Static_assert(BORING_SPAWN_STACK_PAGES <= BORING_ELF_MAX_STACK_PAGES,
               "spawn stack exceeds the bounded ELF ownership table");

bool boring_spawn_stack_layout(uintptr_t base, uintptr_t top,
                                size_t argc, size_t argument_bytes,
                                uintptr_t *rsp, size_t *block_size) {
    size_t size;
    size_t aligned;
    if ((rsp == NULL) || (block_size == NULL) || (top <= base) ||
        ((base & (BORING_ELF_PAGE_SIZE - 1U)) != 0U) ||
        ((top & (BORING_ELF_PAGE_SIZE - 1U)) != 0U) ||
        (top - base > BORING_SPAWN_STACK_SIZE) ||
        (argc == 0U) || (argc > BORING_SYSCALL_ARG_MAX) ||
        (argument_bytes < argc) ||
        (argument_bytes > BORING_SYSCALL_ARG_BYTES_MAX)) {
        return false;
    }
    size = (argc + 1U) * sizeof(uint64_t) + argument_bytes;
    aligned = (size + 15U) & ~(size_t)15U;
    if ((aligned > top - base) ||
        (BORING_SPAWN_STACK_RESERVE > top - base - aligned)) {
        return false;
    }
    *rsp = top - aligned;
    *block_size = size;
    return true;
}
