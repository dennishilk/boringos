#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


# Core review fixes: a permission-query failure on the final mapped page must
# still roll back rather than accidentally satisfying mapped == page_count.
replace_once(
    "kernel/core/user_memory.c",
    "    uint32_t index;\n    uint64_t vector_bytes;\n\n    if (!process_ready(process)) {",
    "    uint32_t index;\n    uint64_t vector_bytes;\n    bool mapping_verified = true;\n\n    if (!process_ready(process)) {",
)
replace_once(
    "kernel/core/user_memory.c",
    "            (info.physical_address != frames[index]) || !info.writable ||\n            info.executable) {\n            break;\n        }\n    }\n    if ((allocated != page_count) || (mapped != page_count)) {",
    "            (info.physical_address != frames[index]) || !info.writable ||\n            info.executable) {\n            mapping_verified = false;\n            break;\n        }\n    }\n    if ((allocated != page_count) || (mapped != page_count) ||\n        !mapping_verified) {",
)
replace_once(
    "kernel/core/user_memory.c",
    "    uint32_t mapped = 0U;\n    uint32_t index;\n\n    if (!process_ready(process)) {",
    "    uint32_t mapped = 0U;\n    uint32_t index;\n    bool mapping_verified = true;\n\n    if (!process_ready(process)) {",
)
replace_once(
    "kernel/core/user_memory.c",
    "            (info.physical_address != object->frames[index]) ||\n            !info.writable || info.executable) {\n            break;\n        }\n    }\n    if (mapped != object->page_count) {",
    "            (info.physical_address != object->frames[index]) ||\n            !info.writable || info.executable) {\n            mapping_verified = false;\n            break;\n        }\n    }\n    if ((mapped != object->page_count) || !mapping_verified) {",
)
replace_once(
    "user/runtime/memory.c",
    "static bool heap_physically_adjacent(const struct boring_heap_block *first,\n                                     const struct boring_heap_block *second) {\n    uintptr_t first_end;\n\n    if (!heap_same_arena(first, second) ||\n        (first->size > UINTPTR_MAX - (uintptr_t)heap_payload(\n             (struct boring_heap_block *)(uintptr_t)first))) {\n        return false;\n    }\n    first_end = (uintptr_t)(const void *)(first + 1) + (uintptr_t)first->size;\n    return first_end == (uintptr_t)(const void *)second;\n}",
    "static bool heap_physically_adjacent(const struct boring_heap_block *first,\n                                     const struct boring_heap_block *second) {\n    uintptr_t payload;\n    uintptr_t first_end;\n\n    if (!heap_same_arena(first, second)) {\n        return false;\n    }\n    payload = (uintptr_t)(const void *)(first + 1);\n    if ((uintptr_t)first->size > UINTPTR_MAX - payload) {\n        return false;\n    }\n    first_end = payload + (uintptr_t)first->size;\n    return first_end == (uintptr_t)(const void *)second;\n}",
)

# Process lifetime integration.
replace_once(
    "kernel/core/process.c",
    "    if (process->cwd_valid) {\n        (void)vfs_path_release(&process->cwd);\n    }\n    process->pid = 0ULL;",
    "    if (process->cwd_valid) {\n        (void)vfs_path_release(&process->cwd);\n    }\n    user_memory_process_state_init(&process->user_memory);\n    process->pid = 0ULL;",
)
replace_once(
    "kernel/core/process.c",
    "    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {\n        process_clear(&processes[index]);\n    }\n    if (!address_space_system_init(&bootstrap_process.address_space)) {",
    "    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {\n        process_clear(&processes[index]);\n    }\n    if (!user_memory_system_init() ||\n        !address_space_system_init(&bootstrap_process.address_space)) {",
)
replace_once(
    "kernel/core/process.c",
    "        (created_process_count == 0ULL) ||\n        !address_space_destroy(&process->address_space)) {",
    "        (created_process_count == 0ULL) ||\n        !user_memory_process_state_empty(&process->user_memory) ||\n        !address_space_destroy(&process->address_space)) {",
)
replace_once(
    "kernel/core/process.c",
    "        (process->state != PROCESS_ALIVE) || (current_process == process) ||\n        !kernel_fd_table_destroy(&process->fd_table)) {",
    "        (process->state != PROCESS_ALIVE) || (current_process == process) ||\n        !user_memory_process_state_empty(&process->user_memory) ||\n        !kernel_fd_table_destroy(&process->fd_table)) {",
)
replace_once(
    "kernel/core/process.c",
    "        (process->state != PROCESS_FINISHED) || (current_process == process) ||\n        !address_space_destroy(&process->address_space)) {",
    "        (process->state != PROCESS_FINISHED) || (current_process == process) ||\n        !user_memory_process_state_empty(&process->user_memory) ||\n        !address_space_destroy(&process->address_space)) {",
)

# Kernel syscall integration.
replace_once(
    "kernel/core/syscall.c",
    "#include <boring/timer.h>\n#include <boring/vfs.h>",
    "#include <boring/timer.h>\n#include <boring/user_memory.h>\n#include <boring/vfs.h>",
)
replace_once(
    "kernel/core/syscall.c",
    "_Static_assert(VFS_ACCESS_WRITE == BORING_FD_OPEN_WRITE,\n               \"descriptor write flag ABI mismatch\");",
    "_Static_assert(VFS_ACCESS_WRITE == BORING_FD_OPEN_WRITE,\n               \"descriptor write flag ABI mismatch\");\n_Static_assert(USER_MEMORY_PAGE_SIZE == BORING_MEMORY_PAGE_SIZE,\n               \"M32 page-size ABI mismatch\");\n_Static_assert(USER_MEMORY_ANON_MAX_BYTES == BORING_MEMORY_ALLOC_MAX_BYTES,\n               \"M32 anonymous allocation bound mismatch\");\n_Static_assert(USER_MEMORY_BUFFER_MAX_BYTES == BORING_BUFFER_MAX_BYTES,\n               \"M32 shared-buffer bound mismatch\");\n_Static_assert(USER_MEMORY_ALLOCATION_MAX == BORING_MEMORY_ALLOCATION_MAX,\n               \"M32 allocation-slot bound mismatch\");\n_Static_assert(USER_MEMORY_BUFFER_HANDLE_MAX == BORING_BUFFER_HANDLE_MAX,\n               \"M32 handle-slot bound mismatch\");\n_Static_assert(USER_MEMORY_BUFFER_MAPPING_MAX == BORING_BUFFER_MAPPING_MAX,\n               \"M32 mapping-slot bound mismatch\");\n_Static_assert(USER_MEMORY_BUFFER_OBJECT_MAX == BORING_BUFFER_OBJECT_MAX,\n               \"M32 object-table bound mismatch\");",
)

memory_syscalls = r'''static int syscall_user_memory_error(enum user_memory_result result) {
    switch (result) {
        case USER_MEMORY_RESULT_OK:
            return 0;
        case USER_MEMORY_RESULT_INVALID:
            return BORING_SYSCALL_EINVAL;
        case USER_MEMORY_RESULT_NO_SPACE:
            return BORING_SYSCALL_ENOSPC;
        case USER_MEMORY_RESULT_NO_MEMORY:
            return BORING_SYSCALL_ENOMEM;
        case USER_MEMORY_RESULT_INTERNAL:
        case USER_MEMORY_RESULT_NOT_INITIALIZED:
        default:
            return BORING_SYSCALL_EIO;
    }
}

static uint64_t syscall_memory_alloc(uint64_t raw_size) {
    struct process *const process = process_current();
    uintptr_t base = 0U;
    enum user_memory_result result;

    if ((raw_size == 0ULL) || (raw_size > (uint64_t)SIZE_MAX) ||
        (process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_memory_allocate(process, (size_t)raw_size, &base);
    if (result != USER_MEMORY_RESULT_OK) {
        return syscall_error(syscall_user_memory_error(result));
    }
    return (uint64_t)base;
}

static uint64_t syscall_memory_free(uint64_t raw_base) {
    struct process *const process = process_current();
    enum user_memory_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_memory_free(process, (uintptr_t)raw_base);
    return (result == USER_MEMORY_RESULT_OK) ? 0ULL :
        syscall_error(syscall_user_memory_error(result));
}

static uint64_t syscall_buffer_create(uint64_t raw_size) {
    struct process *const process = process_current();
    uint32_t handle = BORING_BUFFER_HANDLE_INVALID;
    enum user_memory_result result;

    if ((raw_size == 0ULL) || (raw_size > (uint64_t)SIZE_MAX) ||
        (process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_create(process, (size_t)raw_size, &handle);
    if (result != USER_MEMORY_RESULT_OK) {
        return syscall_error(syscall_user_memory_error(result));
    }
    return (uint64_t)handle;
}

static uint64_t syscall_buffer_map(uint64_t raw_handle) {
    struct process *const process = process_current();
    uintptr_t base = 0U;
    enum user_memory_result result;

    if ((raw_handle > (uint64_t)UINT32_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_map(process, (uint32_t)raw_handle, &base);
    if (result != USER_MEMORY_RESULT_OK) {
        return syscall_error(syscall_user_memory_error(result));
    }
    return (uint64_t)base;
}

static uint64_t syscall_buffer_unmap(uint64_t raw_base) {
    struct process *const process = process_current();
    enum user_memory_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_unmap(process, (uintptr_t)raw_base);
    return (result == USER_MEMORY_RESULT_OK) ? 0ULL :
        syscall_error(syscall_user_memory_error(result));
}

static uint64_t syscall_buffer_close(uint64_t raw_handle) {
    struct process *const process = process_current();
    enum user_memory_result result;

    if ((raw_handle > (uint64_t)UINT32_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_close(process, (uint32_t)raw_handle);
    return (result == USER_MEMORY_RESULT_OK) ? 0ULL :
        syscall_error(syscall_user_memory_error(result));
}

'''
replace_once(
    "kernel/core/syscall.c",
    "static uint64_t syscall_getcwd(uint64_t user_buffer, uint64_t capacity) {",
    memory_syscalls + "static uint64_t syscall_getcwd(uint64_t user_buffer, uint64_t capacity) {",
)
replace_once(
    "kernel/core/syscall.c",
    "    uint64_t child_pid;\n    bool input_released = false;",
    "    uint64_t child_pid;\n    struct user_memory_cleanup_stats memory_cleanup;\n    bool input_released = false;",
)
replace_once(
    "kernel/core/syscall.c",
    "    launch->exit_status = (int32_t)raw_status;\n    (void)boring_input_process_teardown(child_pid, &input_released);\n\n    if (!process_activate(parent)) {",
    "    launch->exit_status = (int32_t)raw_status;\n    (void)boring_input_process_teardown(child_pid, &input_released);\n    if (user_memory_process_cleanup(child, &memory_cleanup) !=\n        USER_MEMORY_RESULT_OK) {\n        syscall_fatal(\"cannot release child M32 memory during SYS_EXIT\");\n    }\n\n    if (!process_activate(parent)) {",
)
replace_once(
    "kernel/core/syscall.c",
    "    if (input_released) {\n        serial_write_string(\"boring-input: owner pid \" );\n        serial_write_u64(child_pid);\n        serial_write_string(\" teardown released\\n\");\n    }\n    *frame = launch->parent_frame;",
    "    if (input_released) {\n        serial_write_string(\"boring-input: owner pid \" );\n        serial_write_u64(child_pid);\n        serial_write_string(\" teardown released\\n\");\n    }\n    serial_write_string(\"boring-memory: cleanup pid \" );\n    serial_write_u64(child_pid);\n    serial_write_string(\" allocations \" );\n    serial_write_u64((uint64_t)memory_cleanup.allocations_released);\n    serial_write_string(\" mappings \" );\n    serial_write_u64((uint64_t)memory_cleanup.mappings_released);\n    serial_write_string(\" handles \" );\n    serial_write_u64((uint64_t)memory_cleanup.handles_released);\n    serial_write_string(\" objects \" );\n    serial_write_u64((uint64_t)memory_cleanup.objects_before);\n    serial_write_string(\"->\" );\n    serial_write_u64((uint64_t)memory_cleanup.objects_after);\n    serial_write_string(\"\\n\");\n    *frame = launch->parent_frame;",
)
replace_once(
    "kernel/core/syscall.c",
    "        case BORING_SYS_INPUT_RELEASE:\n            result = syscall_input_release();\n            break;\n        default:",
    "        case BORING_SYS_INPUT_RELEASE:\n            result = syscall_input_release();\n            break;\n        case BORING_SYS_MEMORY_ALLOC:\n            result = syscall_memory_alloc(frame->rdi);\n            break;\n        case BORING_SYS_MEMORY_FREE:\n            result = syscall_memory_free(frame->rdi);\n            break;\n        case BORING_SYS_BUFFER_CREATE:\n            result = syscall_buffer_create(frame->rdi);\n            break;\n        case BORING_SYS_BUFFER_MAP:\n            result = syscall_buffer_map(frame->rdi);\n            break;\n        case BORING_SYS_BUFFER_UNMAP:\n            result = syscall_buffer_unmap(frame->rdi);\n            break;\n        case BORING_SYS_BUFFER_CLOSE:\n            result = syscall_buffer_close(frame->rdi);\n            break;\n        default:",
)

# Userspace syscall wrappers.
runtime_wrappers = r'''

long boring_memory_alloc_raw(size_t size) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_MEMORY_ALLOC), "D"(size)
        : "rcx", "r11", "cc", "memory");
    return result;
}

void *boring_memory_alloc(size_t size) {
    const long result = boring_memory_alloc_raw(size);

    return (result > 0L) ? (void *)(uintptr_t)result : NULL;
}

long boring_memory_free(void *base) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_MEMORY_FREE), "D"(base)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_buffer_create(size_t size) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_CREATE), "D"(size)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_buffer_map_raw(uint32_t handle) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_MAP), "D"((uint64_t)handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}

void *boring_buffer_map(uint32_t handle) {
    const long result = boring_buffer_map_raw(handle);

    return (result > 0L) ? (void *)(uintptr_t)result : NULL;
}

long boring_buffer_unmap(void *base) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_UNMAP), "D"(base)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_buffer_close(uint32_t handle) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_CLOSE), "D"((uint64_t)handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}
'''
p = Path("user/runtime/syscall.c")
text = p.read_text()
if "long boring_memory_alloc_raw(size_t size)" in text:
    raise SystemExit("user/runtime/syscall.c: M32 wrappers already present")
p.write_text(text + runtime_wrappers)

# Makefile integration.
replace_once(
    "Makefile",
    "INPUT_TEST_ELF := $(USER_BUILD_DIR)/input-test.elf\nISO :=",
    "INPUT_TEST_ELF := $(USER_BUILD_DIR)/input-test.elf\nMEMORY_TEST_MAIN_OBJECT := $(USER_BUILD_DIR)/memory-test/main.o\nMEMORY_TEST_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(MEMORY_TEST_MAIN_OBJECT)\nMEMORY_TEST_ELF := $(USER_BUILD_DIR)/memory-test.elf\nISO :=",
)
replace_once(
    "Makefile",
    "INPUT_HOST_TEST := $(BUILD_DIR)/input-host-test\nMKBORINGFS_VERIFY :=",
    "INPUT_HOST_TEST := $(BUILD_DIR)/input-host-test\nMEMORY_HOST_TEST := $(BUILD_DIR)/memory-host-test\nRUNTIME_HEAP_HOST_TEST := $(BUILD_DIR)/runtime-heap-host-test\nMKBORINGFS_VERIFY :=",
)
replace_once(
    "Makefile",
    "INPUT_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \\\n\t-T user/input-test/linker.ld\n",
    "INPUT_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \\\n\t-T user/input-test/linker.ld\nMEMORY_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \\\n\t-T user/memory-test/linker.ld\n",
)
replace_once(
    "Makefile",
    "\tkernel/core/input.c \\\n\tkernel/core/vfs.c \\",
    "\tkernel/core/input.c \\\n\tkernel/core/user_memory.c \\\n\tkernel/core/vfs.c \\",
)
replace_once(
    "Makefile",
    ".PHONY: all kernel user-elf user-runtime user-console user-init user-shell user-boringfetch user-cat user-input-test elf-audit runtime-audit console-audit init-audit shell-audit boringfetch-audit cat-audit input-test-audit shell-host-test fd-host-test framebuffer-host-test input-host-test boringfs-host-test boringfs-vfs-host-test mkboringfs mkboringfs-test boringfsck boringfsck-test boringfs-fixture qemu-bundle run run-headless test clean distclean",
    ".PHONY: all kernel user-elf user-runtime user-console user-init user-shell user-boringfetch user-cat user-input-test user-memory-test elf-audit runtime-audit console-audit init-audit shell-audit boringfetch-audit cat-audit input-test-audit memory-test-audit shell-host-test fd-host-test framebuffer-host-test input-host-test memory-host-test boringfs-host-test boringfs-vfs-host-test mkboringfs mkboringfs-test boringfsck boringfsck-test boringfs-fixture qemu-bundle run run-headless test clean distclean",
)
replace_once(
    "Makefile",
    "user-input-test: $(INPUT_TEST_ELF)\n\nelf-audit:",
    "user-input-test: $(INPUT_TEST_ELF)\n\nuser-memory-test: $(MEMORY_TEST_ELF)\n\nelf-audit:",
)
replace_once(
    "Makefile",
    "input-test-audit: $(INPUT_TEST_ELF)\n\tsh ./tests/input-test-build-audit.sh\n\nshell-host-test:",
    "input-test-audit: $(INPUT_TEST_ELF)\n\tsh ./tests/input-test-build-audit.sh\n\nmemory-test-audit: $(MEMORY_TEST_ELF)\n\tsh ./tests/memory-test-build-audit.sh\n\nshell-host-test:",
)
replace_once(
    "Makefile",
    "input-host-test: $(INPUT_HOST_TEST)\n\t$(INPUT_HOST_TEST)\n\nboringfs-host-test:",
    "input-host-test: $(INPUT_HOST_TEST)\n\t$(INPUT_HOST_TEST)\n\nmemory-host-test: $(MEMORY_HOST_TEST) $(RUNTIME_HEAP_HOST_TEST)\n\t$(MEMORY_HOST_TEST)\n\t$(RUNTIME_HEAP_HOST_TEST)\n\nboringfs-host-test:",
)
replace_once(
    "Makefile",
    "\t$(MAKE) user-input-test\n\tmkdir -p $(BUILD_DIR)/boringos-qemu-x86_64",
    "\t$(MAKE) user-input-test\n\t$(MAKE) user-memory-test\n\tmkdir -p $(BUILD_DIR)/boringos-qemu-x86_64",
)
replace_once(
    "Makefile",
    "\t$(BORINGFS_FIXTURE) $(BUILD_DIR)/boringos-qemu-x86_64/boringos-root.img valid $(BORINGFETCH_ELF) $(CAT_ELF) $(INPUT_TEST_ELF)\n",
    "\t$(BORINGFS_FIXTURE) $(BUILD_DIR)/boringos-qemu-x86_64/boringos-root.img valid $(BORINGFETCH_ELF) $(CAT_ELF) $(INPUT_TEST_ELF) $(MEMORY_TEST_ELF)\n",
)
replace_once(
    "Makefile",
    "$(INPUT_HOST_TEST): tests/input-host-test.c kernel/core/input.c \\\n\t\tkernel/arch/x86_64/ps2_keyboard.c kernel/arch/x86_64/ps2_mouse.c \\\n\t\tkernel/include/boring/input.h kernel/include/boring/input_abi.h \\\n\t\tkernel/include/boring/ps2_keyboard.h kernel/include/boring/ps2_mouse.h\n\t@mkdir -p $(dir $@)\n\t$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \\\n\t\ttests/input-host-test.c kernel/core/input.c \\\n\t\tkernel/arch/x86_64/ps2_keyboard.c kernel/arch/x86_64/ps2_mouse.c -o $@\n",
    "$(INPUT_HOST_TEST): tests/input-host-test.c kernel/core/input.c \\\n\t\tkernel/arch/x86_64/ps2_keyboard.c kernel/arch/x86_64/ps2_mouse.c \\\n\t\tkernel/include/boring/input.h kernel/include/boring/input_abi.h \\\n\t\tkernel/include/boring/ps2_keyboard.h kernel/include/boring/ps2_mouse.h\n\t@mkdir -p $(dir $@)\n\t$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \\\n\t\ttests/input-host-test.c kernel/core/input.c \\\n\t\tkernel/arch/x86_64/ps2_keyboard.c kernel/arch/x86_64/ps2_mouse.c -o $@\n\n$(MEMORY_HOST_TEST): tests/memory-host-test.c kernel/core/user_memory.c \\\n\t\tkernel/include/boring/user_memory.h kernel/include/boring/process.h \\\n\t\tkernel/include/boring/ring3_memory.h kernel/include/boring/address_space.h\n\t@mkdir -p $(dir $@)\n\t$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \\\n\t\ttests/memory-host-test.c kernel/core/user_memory.c -o $@\n\n$(RUNTIME_HEAP_HOST_TEST): tests/runtime-heap-host-test.c user/runtime/memory.c \\\n\t\tuser/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h \\\n\t\tkernel/include/boring/syscall_abi.h\n\t@mkdir -p $(dir $@)\n\t$(HOST_CC) -Iuser/runtime/include -Ikernel/include $(HOST_CFLAGS) \\\n\t\ttests/runtime-heap-host-test.c user/runtime/memory.c -o $@\n",
)
replace_once(
    "Makefile",
    "$(RUNTIME_MEMORY_OBJECT): user/runtime/memory.c user/runtime/include/boring/memory.h\n",
    "$(RUNTIME_MEMORY_OBJECT): user/runtime/memory.c user/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h kernel/include/boring/syscall_abi.h\n",
)
replace_once(
    "Makefile",
    "$(INPUT_TEST_ELF): $(INPUT_TEST_OBJECTS) user/input-test/linker.ld\n\t@mkdir -p $(dir $@)\n\t$(LD) $(INPUT_TEST_LDFLAGS) $(INPUT_TEST_OBJECTS) -o $@\n",
    "$(INPUT_TEST_ELF): $(INPUT_TEST_OBJECTS) user/input-test/linker.ld\n\t@mkdir -p $(dir $@)\n\t$(LD) $(INPUT_TEST_LDFLAGS) $(INPUT_TEST_OBJECTS) -o $@\n\n$(MEMORY_TEST_MAIN_OBJECT): user/memory-test/main.c user/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h\n\t@mkdir -p $(dir $@)\n\t$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@\n\n$(MEMORY_TEST_ELF): $(MEMORY_TEST_OBJECTS) user/memory-test/linker.ld\n\t@mkdir -p $(dir $@)\n\t$(LD) $(MEMORY_TEST_LDFLAGS) $(MEMORY_TEST_OBJECTS) -o $@\n",
)
replace_once(
    "Makefile",
    "\tsh ./tests/input-test-build-audit.sh\n\t$(MAKE) shell-host-test",
    "\tsh ./tests/input-test-build-audit.sh\n\tsh ./tests/memory-test-build-audit.sh\n\t$(MAKE) shell-host-test",
)
replace_once(
    "Makefile",
    "\t$(MAKE) input-host-test\n\tsh ./tests/shell-qemu.sh",
    "\t$(MAKE) input-host-test\n\t$(MAKE) memory-host-test\n\tsh ./tests/shell-qemu.sh",
)
replace_once(
    "Makefile",
    "\tsh ./tests/input-qemu.sh\n\tsh ./tests/framebuffer-qemu.sh",
    "\tsh ./tests/input-qemu.sh\n\tsh ./tests/memory-qemu.sh\n\tsh ./tests/framebuffer-qemu.sh",
)

# Persistent BoringFS fixture: grow only the test image geometry, not the v0
# on-disk format, and add the fourth fixed /bin slot.
replace_once("tests/boringfs-fixture.c", "#define FIXTURE_BLOCKS 64U", "#define FIXTURE_BLOCKS 80U")
replace_once(
    "tests/boringfs-fixture.c",
    "#define INPUT_TEST_BLOCK0 (CAT_BLOCK0 + PROGRAM_SLOT_BLOCKS)\n#define ARCH_SIZE",
    "#define INPUT_TEST_BLOCK0 (CAT_BLOCK0 + PROGRAM_SLOT_BLOCKS)\n#define MEMORY_TEST_BLOCK0 (INPUT_TEST_BLOCK0 + PROGRAM_SLOT_BLOCKS)\n#define ARCH_SIZE",
)
replace_once(
    "tests/boringfs-fixture.c",
    "                        const uint8_t *input_test_bytes,\n                        size_t input_test_size) {",
    "                        const uint8_t *input_test_bytes,\n                        size_t input_test_size,\n                        const uint8_t *memory_test_bytes,\n                        size_t memory_test_size) {",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    uint32_t input_test_blocks = 0U;\n    size_t index;\n    const bool have_boringfetch = (boringfetch_bytes != NULL);\n    const bool have_cat = (cat_bytes != NULL);\n    const bool have_input_test = (input_test_bytes != NULL);",
    "    uint32_t input_test_blocks = 0U;\n    uint32_t memory_test_blocks = 0U;\n    size_t index;\n    const bool have_boringfetch = (boringfetch_bytes != NULL);\n    const bool have_cat = (cat_bytes != NULL);\n    const bool have_input_test = (input_test_bytes != NULL);\n    const bool have_memory_test = (memory_test_bytes != NULL);",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    if ((have_cat && !have_boringfetch) ||\n        (have_input_test && !have_cat)) {",
    "    if ((have_cat && !have_boringfetch) ||\n        (have_input_test && !have_cat) ||\n        (have_memory_test && !have_input_test)) {",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    } else if (input_test_size != 0U) {\n        return false;\n    }\n\n    if (volume_size",
    "    } else if (input_test_size != 0U) {\n        return false;\n    }\n    if (have_memory_test) {\n        if (!fixture_program_blocks(memory_test_size, MEMORY_TEST_BLOCK0,\n                                    &memory_test_blocks)) {\n            return false;\n        }\n    } else if (memory_test_size != 0U) {\n        return false;\n    }\n\n    if (volume_size",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    if (have_input_test) {\n        for (block = INPUT_TEST_BLOCK0;\n             block < INPUT_TEST_BLOCK0 + input_test_blocks; ++block) {\n            bitmap_set(volume, block, true);\n        }\n    }\n\n    extent[0].start_block",
    "    if (have_input_test) {\n        for (block = INPUT_TEST_BLOCK0;\n             block < INPUT_TEST_BLOCK0 + input_test_blocks; ++block) {\n            bitmap_set(volume, block, true);\n        }\n    }\n    if (have_memory_test) {\n        for (block = MEMORY_TEST_BLOCK0;\n             block < MEMORY_TEST_BLOCK0 + memory_test_blocks; ++block) {\n            bitmap_set(volume, block, true);\n        }\n    }\n\n    extent[0].start_block",
)
replace_once(
    "tests/boringfs-fixture.c",
    "                         (have_input_test ? 3ULL :\n                          (have_cat ? 2ULL : 1ULL)) *",
    "                         (have_memory_test ? 4ULL :\n                          (have_input_test ? 3ULL :\n                           (have_cat ? 2ULL : 1ULL))) *",
)
replace_once(
    "tests/boringfs-fixture.c",
    "        if (have_input_test) {\n            extent[0].start_block = INPUT_TEST_BLOCK0;\n            extent[0].block_count = input_test_blocks;\n            if (!make_object(volume, volume_size, &superblock, 14U, 11U,\n                             BORINGFS_TYPE_REGULAR,\n                             (uint64_t)input_test_size, extent, 1U)) {\n                return false;\n            }\n        }\n    }",
    "        if (have_input_test) {\n            extent[0].start_block = INPUT_TEST_BLOCK0;\n            extent[0].block_count = input_test_blocks;\n            if (!make_object(volume, volume_size, &superblock, 14U, 11U,\n                             BORINGFS_TYPE_REGULAR,\n                             (uint64_t)input_test_size, extent, 1U)) {\n                return false;\n            }\n        }\n        if (have_memory_test) {\n            extent[0].start_block = MEMORY_TEST_BLOCK0;\n            extent[0].block_count = memory_test_blocks;\n            if (!make_object(volume, volume_size, &superblock, 15U, 11U,\n                             BORINGFS_TYPE_REGULAR,\n                             (uint64_t)memory_test_size, extent, 1U)) {\n                return false;\n            }\n        }\n    }",
)
replace_once(
    "tests/boringfs-fixture.c",
    "         (have_input_test &&\n          !write_dirent(volume, volume_size, BIN_BLOCK, 2ULL, 14U,\n                        BORINGFS_TYPE_REGULAR, \"input-test\")))) {",
    "         (have_input_test &&\n          !write_dirent(volume, volume_size, BIN_BLOCK, 2ULL, 14U,\n                        BORINGFS_TYPE_REGULAR, \"input-test\")) ||\n         (have_memory_test &&\n          !write_dirent(volume, volume_size, BIN_BLOCK, 3ULL, 15U,\n                        BORINGFS_TYPE_REGULAR, \"memory-test\")))) {",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    if (have_input_test) {\n        (void)memcpy(&volume[(size_t)INPUT_TEST_BLOCK0 * BORINGFS_BLOCK_SIZE],\n                     input_test_bytes, input_test_size);\n    }\n    for (index",
    "    if (have_input_test) {\n        (void)memcpy(&volume[(size_t)INPUT_TEST_BLOCK0 * BORINGFS_BLOCK_SIZE],\n                     input_test_bytes, input_test_size);\n    }\n    if (have_memory_test) {\n        (void)memcpy(&volume[(size_t)MEMORY_TEST_BLOCK0 * BORINGFS_BLOCK_SIZE],\n                     memory_test_bytes, memory_test_size);\n    }\n    for (index",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    uint8_t *input_test_bytes = NULL;\n    size_t boringfetch_size = 0U;\n    size_t cat_size = 0U;\n    size_t input_test_size = 0U;",
    "    uint8_t *input_test_bytes = NULL;\n    uint8_t *memory_test_bytes = NULL;\n    size_t boringfetch_size = 0U;\n    size_t cat_size = 0U;\n    size_t input_test_size = 0U;\n    size_t memory_test_size = 0U;",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    if ((argc < 3) || (argc > 6)) {\n        (void)fprintf(stderr,\n                      \"usage: %s <output> <valid|bad-magic|bad-geometry|bad-bitmap|bad-object|bad-extent|bad-directory> [boringfetch-elf [cat-elf [input-test-elf]]]\\n\",",
    "    if ((argc < 3) || (argc > 7)) {\n        (void)fprintf(stderr,\n                      \"usage: %s <output> <valid|bad-magic|bad-geometry|bad-bitmap|bad-object|bad-extent|bad-directory> [boringfetch-elf [cat-elf [input-test-elf [memory-test-elf]]]]\\n\",",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    if ((argc == 6) &&\n        !read_program(argv[5], &input_test_bytes, &input_test_size)) {",
    "    if ((argc >= 6) &&\n        !read_program(argv[5], &input_test_bytes, &input_test_size)) {",
)
replace_once(
    "tests/boringfs-fixture.c",
    "        return 2;\n    }\n    volume = (uint8_t *)malloc(volume_size);",
    "        return 2;\n    }\n    if ((argc == 7) &&\n        !read_program(argv[6], &memory_test_bytes, &memory_test_size)) {\n        free(input_test_bytes);\n        free(cat_bytes);\n        free(boringfetch_bytes);\n        (void)fprintf(stderr, \"cannot read bounded memory-test ELF: %s\\n\", argv[6]);\n        return 2;\n    }\n    volume = (uint8_t *)malloc(volume_size);",
)
replace_once(
    "tests/boringfs-fixture.c",
    "                     cat_bytes, cat_size,\n                     input_test_bytes, input_test_size)) {\n        free(input_test_bytes);",
    "                     cat_bytes, cat_size,\n                     input_test_bytes, input_test_size,\n                     memory_test_bytes, memory_test_size)) {\n        free(memory_test_bytes);\n        free(input_test_bytes);",
)
replace_once(
    "tests/boringfs-fixture.c",
    "    free(input_test_bytes);\n    free(cat_bytes);",
    "    free(memory_test_bytes);\n    free(input_test_bytes);\n    free(cat_bytes);",
)

# Permanent workflow additions while preserving every historical step.
replace_once(
    ".github/workflows/boot-test.yml",
    "      - name: Standalone input-test ELF audit\n        run: sh ./tests/input-test-build-audit.sh\n\n      - name: Audit generated native C runtime artifact",
    "      - name: Standalone input-test ELF audit\n        run: sh ./tests/input-test-build-audit.sh\n\n      - name: Standalone memory-test ELF audit\n        run: sh ./tests/memory-test-build-audit.sh\n\n      - name: Audit generated native C runtime artifact",
)
replace_once(
    ".github/workflows/boot-test.yml",
    "      - name: Native input decoder and queue host tests\n        run: make input-host-test\n\n      - name: Build boring-shell test mode",
    "      - name: Native input decoder and queue host tests\n        run: make input-host-test\n\n      - name: M32 userspace memory shared-buffer and heap host tests\n        run: make memory-host-test\n\n      - name: Build boring-shell test mode",
)
replace_once(
    ".github/workflows/boot-test.yml",
    "      - name: Real PS/2 keyboard and mouse userspace input\n        run: sh ./tests/input-qemu.sh\n\n      - name: Real Limine framebuffer and BoringOS dashboard",
    "      - name: Real PS/2 keyboard and mouse userspace input\n        run: sh ./tests/input-qemu.sh\n\n      - name: Real Ring-3 userspace memory and shared buffers\n        run: sh ./tests/memory-qemu.sh\n\n      - name: Real Limine framebuffer and BoringOS dashboard",
)

print("M32 deterministic integration edits applied")
