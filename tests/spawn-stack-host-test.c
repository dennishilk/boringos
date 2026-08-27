#include <stdio.h>
#include <stdlib.h>
#include <boring/spawn_stack.h>

static size_t checks;
static void require(bool condition) {
    ++checks;
    if (!condition) { (void)fprintf(stderr, "spawn stack check %zu failed\n", checks); exit(1); }
}

int main(void) {
    const uintptr_t base = BORING_SPAWN_STACK_BASE;
    const uintptr_t top = base + BORING_SPAWN_STACK_SIZE;
    uintptr_t rsp = 0U;
    size_t size = 0U;
    size_t argc;
    size_t bytes;
    for (argc = 1U; argc <= BORING_SYSCALL_ARG_MAX; ++argc) {
        for (bytes = argc; bytes <= BORING_SYSCALL_ARG_BYTES_MAX; ++bytes) {
            require(boring_spawn_stack_layout(base, top, argc, bytes, &rsp, &size));
            require((rsp & 15U) == 0U && rsp >= base + BORING_SPAWN_STACK_RESERVE);
            require(size == (argc + 1U) * sizeof(uint64_t) + bytes && rsp + size <= top);
        }
    }
    /* Exactly the old one-page contract must fail, even with a tiny argv. */
    require(!boring_spawn_stack_layout(base, base + BORING_ELF_PAGE_SIZE, 1U, 1U, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top, 0U, 1U, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top, 17U, 17U, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top, 16U, 15U, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top, 1U, 1025U, &rsp, &size));
    require(!boring_spawn_stack_layout(base + 1U, top, 1U, 1U, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top - 1U, 1U, 1U, &rsp, &size));
    require(!boring_spawn_stack_layout(top, base, 1U, 1U, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top + BORING_ELF_PAGE_SIZE, 1U, 1U, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top, SIZE_MAX, SIZE_MAX, &rsp, &size));
    require(!boring_spawn_stack_layout(base, top, 1U, 1U, NULL, &size));
    require(!boring_spawn_stack_layout(base, top, 1U, 1U, &rsp, NULL));
    require(boring_spawn_stack_layout(base, top, 16U, 1024U, &rsp, &size));
    require(top - rsp == 1168U && rsp - base == 7024U);
    (void)printf("M36 spawn stack: %zu checks passed.\n", checks);
    return 0;
}
