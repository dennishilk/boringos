#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, marker: str | None = None) -> None:
    p = Path(path)
    text = p.read_text()
    if marker is not None and marker in text:
        return
    if old not in text:
        raise SystemExit(f"anchor missing in {path}: {old[:100]!r}")
    if text.count(old) != 1:
        raise SystemExit(f"anchor not unique in {path}: {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


# Preserve the historical global SYSCALL stack and additionally accept only
# the currently running cooperative task's already-bounded kernel stack.
replace_once(
    "kernel/core/syscall.c",
    "#include <boring/timer.h>\n#include <boring/user_memory.h>\n",
    "#include <boring/timer.h>\n#include <boring/task.h>\n#include <boring/user_memory.h>\n",
    "#include <boring/task.h>")
replace_once(
    "kernel/core/syscall.c",
    '''bool syscall_stack_contains(uintptr_t stack_pointer) {
    const uintptr_t base = (uintptr_t)&x86_64_syscall_stack[0];
    const uintptr_t top = base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;

    return (stack_pointer >= base) && (stack_pointer < top);
}
''',
    '''bool syscall_stack_contains(uintptr_t stack_pointer) {
    const uintptr_t base = (uintptr_t)&x86_64_syscall_stack[0];
    const uintptr_t top = base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;

    return ((stack_pointer >= base) && (stack_pointer < top)) ||
           task_current_stack_contains((const void *)stack_pointer);
}

static bool syscall_frame_on_trusted_stack(uintptr_t frame_address) {
    uintptr_t last;

    if (frame_address > UINTPTR_MAX - (uintptr_t)(sizeof(struct x86_64_syscall_frame) - 1U)) {
        return false;
    }
    last = frame_address +
           (uintptr_t)(sizeof(struct x86_64_syscall_frame) - 1U);
    return syscall_stack_contains(frame_address) && syscall_stack_contains(last);
}
''',
    "static bool syscall_frame_on_trusted_stack")
replace_once(
    "kernel/core/syscall.c",
    '''    if (!syscall_initialized || (frame == NULL) ||
        !syscall_stack_contains(live_rsp) ||
        !syscall_stack_contains(frame_address) ||
        (frame_address > syscall_state.stack_top - (uintptr_t)sizeof(*frame))) {
''',
    '''    if (!syscall_initialized || (frame == NULL) ||
        !syscall_stack_contains(live_rsp) ||
        !syscall_frame_on_trusted_stack(frame_address)) {
''',
    "!syscall_frame_on_trusted_stack(frame_address)")

# M33 test mode and kernel harness route.
replace_once(
    "kernel/core/entry.c",
    "#include <boring/exception.h>\n",
    "#include <boring/exception.h>\n#include <boring/elf_boot.h>\n",
    "#include <boring/elf_boot.h>")
replace_once(
    "kernel/core/entry.c",
    "#include <boring/init_test.h>\n",
    "#include <boring/init_test.h>\n#include <boring/ipc_test.h>\n",
    "#include <boring/ipc_test.h>")
replace_once(
    "kernel/core/entry.c",
    "#define BORING_TEST_MODE_PERSISTENT_ROOT 15\n",
    "#define BORING_TEST_MODE_PERSISTENT_ROOT 15\n#define BORING_TEST_MODE_M33_IPC 16\n",
    "#define BORING_TEST_MODE_M33_IPC 16")
replace_once(
    "kernel/core/entry.c",
    '''    (BORING_TEST_MODE != BORING_TEST_MODE_BORINGFS_RW) && \\
    (BORING_TEST_MODE != BORING_TEST_MODE_PERSISTENT_ROOT)
''',
    '''    (BORING_TEST_MODE != BORING_TEST_MODE_BORINGFS_RW) && \\
    (BORING_TEST_MODE != BORING_TEST_MODE_PERSISTENT_ROOT) && \\
    (BORING_TEST_MODE != BORING_TEST_MODE_M33_IPC)
''',
    "BORING_TEST_MODE != BORING_TEST_MODE_M33_IPC)")
replace_once(
    "kernel/core/entry.c",
    '''#elif BORING_TEST_MODE == BORING_TEST_MODE_PERSISTENT_ROOT
    boringfs_ro_test_run();
#endif
''',
    '''#elif BORING_TEST_MODE == BORING_TEST_MODE_PERSISTENT_ROOT
    boringfs_ro_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_M33_IPC
    ipc_test_run(elf_boot_module_response());
#endif
''',
    "ipc_test_run(elf_boot_module_response())")

# Makefile: standalone ipc-test, kernel IPC core, host test and test mode.
replace_once(
    "Makefile",
    "RUNTIME_STRING_OBJECT := $(USER_BUILD_DIR)/runtime/string.o\n",
    "RUNTIME_STRING_OBJECT := $(USER_BUILD_DIR)/runtime/string.o\nIPC_RUNTIME_OBJECT := $(USER_BUILD_DIR)/runtime/ipc.o\n",
    "IPC_RUNTIME_OBJECT :=")
replace_once(
    "Makefile",
    "MEMORY_TEST_ELF := $(USER_BUILD_DIR)/memory-test.elf\n",
    "MEMORY_TEST_ELF := $(USER_BUILD_DIR)/memory-test.elf\n"
    "IPC_TEST_MAIN_OBJECT := $(USER_BUILD_DIR)/ipc-test/main.o\n"
    "IPC_TEST_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(IPC_RUNTIME_OBJECT) $(IPC_TEST_MAIN_OBJECT)\n"
    "IPC_TEST_ELF := $(USER_BUILD_DIR)/ipc-test.elf\n",
    "IPC_TEST_ELF :=")
replace_once(
    "Makefile",
    "RUNTIME_HEAP_HOST_TEST := $(BUILD_DIR)/runtime-heap-host-test\n",
    "RUNTIME_HEAP_HOST_TEST := $(BUILD_DIR)/runtime-heap-host-test\nIPC_HOST_TEST := $(BUILD_DIR)/ipc-host-test\n",
    "IPC_HOST_TEST :=")
replace_once(
    "Makefile",
    '''else ifeq ($(TEST_MODE),persistent-root)
TEST_MODE_VALUE := 15
TEST_HARNESS_C := kernel/core/boringfs_ro_test.c
BOOT_USER_ELF := $(SHELL_INIT_ELF)
BOOT_USER_NAME := boring-init.elf
BOOT_EXTRA_USER_ELF := $(SHELL_ELF)
BOOT_EXTRA_USER_NAME := boring-shell.elf
BOOT_LIMINE_CONF := limine-shell.conf
else
''',
    '''else ifeq ($(TEST_MODE),persistent-root)
TEST_MODE_VALUE := 15
TEST_HARNESS_C := kernel/core/boringfs_ro_test.c
BOOT_USER_ELF := $(SHELL_INIT_ELF)
BOOT_USER_NAME := boring-init.elf
BOOT_EXTRA_USER_ELF := $(SHELL_ELF)
BOOT_EXTRA_USER_NAME := boring-shell.elf
BOOT_LIMINE_CONF := limine-shell.conf
else ifeq ($(TEST_MODE),m33-ipc)
TEST_MODE_VALUE := 16
TEST_HARNESS_C := kernel/core/syscall_test.c
BOOT_USER_ELF := $(IPC_TEST_ELF)
BOOT_USER_NAME := ipc-test.elf
BOOT_LIMINE_CONF := limine-ipc.conf
else
''',
    "else ifeq ($(TEST_MODE),m33-ipc)")
replace_once(
    "Makefile",
    "or persistent-root)\nendif\n",
    "or persistent-root, or m33-ipc)\nendif\n",
    "or persistent-root, or m33-ipc)")
replace_once(
    "Makefile",
    "MEMORY_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \\\n\t-T user/memory-test/linker.ld\n",
    "MEMORY_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \\\n\t-T user/memory-test/linker.ld\n"
    "IPC_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \\\n\t-T user/memory-test/linker.ld\n",
    "IPC_TEST_LDFLAGS :=")
replace_once(
    "Makefile",
    "\tkernel/core/user_memory.c \\\n\tkernel/core/vfs.c \\\n",
    "\tkernel/core/user_memory.c \\\n\tkernel/core/ipc.c \\\n\tkernel/core/ipc_syscall.c \\\n\tkernel/core/ipc_test.c \\\n\tkernel/core/vfs.c \\\n",
    "\tkernel/core/ipc_syscall.c")
replace_once(
    "Makefile",
    "user-memory-test elf-audit",
    "user-memory-test user-ipc-test elf-audit",
    "user-ipc-test elf-audit")
replace_once(
    "Makefile",
    "memory-test-audit shell-host-test",
    "memory-test-audit ipc-test-audit shell-host-test",
    "ipc-test-audit shell-host-test")
replace_once(
    "Makefile",
    "memory-host-test boringfs-host-test",
    "memory-host-test ipc-host-test boringfs-host-test",
    "ipc-host-test boringfs-host-test")
replace_once(
    "Makefile",
    '''user-memory-test: $(MEMORY_TEST_ELF)

elf-audit:''',
    '''user-memory-test: $(MEMORY_TEST_ELF)

user-ipc-test: $(IPC_TEST_ELF)

elf-audit:''',
    "user-ipc-test: $(IPC_TEST_ELF)")
replace_once(
    "Makefile",
    '''memory-test-audit: $(MEMORY_TEST_ELF)
	sh ./tests/memory-test-build-audit.sh

shell-host-test:''',
    '''memory-test-audit: $(MEMORY_TEST_ELF)
	sh ./tests/memory-test-build-audit.sh

ipc-test-audit: $(IPC_TEST_ELF)
	sh ./tests/ipc-test-build-audit.sh

shell-host-test:''',
    "ipc-test-audit: $(IPC_TEST_ELF)")
replace_once(
    "Makefile",
    '''memory-host-test: $(MEMORY_HOST_TEST) $(RUNTIME_HEAP_HOST_TEST)
	$(MEMORY_HOST_TEST)
	$(RUNTIME_HEAP_HOST_TEST)

boringfs-host-test:''',
    '''memory-host-test: $(MEMORY_HOST_TEST) $(RUNTIME_HEAP_HOST_TEST)
	$(MEMORY_HOST_TEST)
	$(RUNTIME_HEAP_HOST_TEST)

ipc-host-test: $(IPC_HOST_TEST)
	$(IPC_HOST_TEST)

boringfs-host-test:''',
    "ipc-host-test: $(IPC_HOST_TEST)")
replace_once(
    "Makefile",
    '''$(RUNTIME_HEAP_HOST_TEST): tests/runtime-heap-host-test.c user/runtime/memory.c \\
		user/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h \\
		kernel/include/boring/syscall_abi.h
''',
    '''$(IPC_HOST_TEST): tests/ipc-host-test.c kernel/core/ipc.c \\
		kernel/include/boring/ipc.h kernel/include/boring/process.h \\
		kernel/include/boring/task.h kernel/include/boring/user_memory.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \\
		tests/ipc-host-test.c kernel/core/ipc.c -o $@

$(RUNTIME_HEAP_HOST_TEST): tests/runtime-heap-host-test.c user/runtime/memory.c \\
		user/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h \\
		kernel/include/boring/syscall_abi.h
''',
    "$(IPC_HOST_TEST): tests/ipc-host-test.c")
replace_once(
    "Makefile",
    '''$(RUNTIME_STRING_OBJECT): user/runtime/string.c user/runtime/include/boring/string.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@
''',
    '''$(RUNTIME_STRING_OBJECT): user/runtime/string.c user/runtime/include/boring/string.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(IPC_RUNTIME_OBJECT): user/runtime/ipc.c user/runtime/include/boring/ipc.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@
''',
    "$(IPC_RUNTIME_OBJECT): user/runtime/ipc.c")
replace_once(
    "Makefile",
    '''$(MEMORY_TEST_ELF): $(MEMORY_TEST_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(MEMORY_TEST_LDFLAGS) $(MEMORY_TEST_OBJECTS) -o $@
''',
    '''$(MEMORY_TEST_ELF): $(MEMORY_TEST_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(MEMORY_TEST_LDFLAGS) $(MEMORY_TEST_OBJECTS) -o $@

$(IPC_TEST_MAIN_OBJECT): user/ipc-test/main.c user/runtime/include/boring/ipc.h user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(IPC_TEST_ELF): $(IPC_TEST_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(IPC_TEST_LDFLAGS) $(IPC_TEST_OBJECTS) -o $@
''',
    "$(IPC_TEST_MAIN_OBJECT): user/ipc-test/main.c")
replace_once(
    "Makefile",
    "\tsh ./tests/memory-test-build-audit.sh\n\t$(MAKE) shell-host-test\n",
    "\tsh ./tests/memory-test-build-audit.sh\n"
    "\tsh ./tests/ipc-test-build-audit.sh\n"
    "\t$(MAKE) ipc-host-test\n"
    "\t$(MAKE) shell-host-test\n",
    "\tsh ./tests/ipc-test-build-audit.sh\n")
replace_once(
    "Makefile",
    "\tsh ./tests/memory-qemu.sh\n\tsh ./tests/framebuffer-qemu.sh\n",
    "\tsh ./tests/memory-qemu.sh\n\tsh ./tests/ipc-qemu.sh\n\tsh ./tests/framebuffer-qemu.sh\n",
    "\tsh ./tests/ipc-qemu.sh\n")

# Permanent CI gates; historical steps are left untouched.
replace_once(
    ".github/workflows/boot-test.yml",
    '''      - name: Standalone memory-test ELF audit
        run: sh ./tests/memory-test-build-audit.sh
''',
    '''      - name: Standalone memory-test ELF audit
        run: sh ./tests/memory-test-build-audit.sh

      - name: Standalone ipc-test ELF audit
        run: sh ./tests/ipc-test-build-audit.sh
''',
    "- name: Standalone ipc-test ELF audit")
replace_once(
    ".github/workflows/boot-test.yml",
    '''      - name: M32 userspace memory shared-buffer and runtime heap host tests
        run: make memory-host-test
''',
    '''      - name: M32 userspace memory shared-buffer and runtime heap host tests
        run: make memory-host-test

      - name: M33 bounded service registry IPC and grant host tests
        run: make ipc-host-test
''',
    "- name: M33 bounded service registry IPC and grant host tests")
replace_once(
    ".github/workflows/boot-test.yml",
    '''      - name: Real userspace memory allocation and shared buffers
        run: sh ./tests/memory-qemu.sh
''',
    '''      - name: Real userspace memory allocation and shared buffers
        run: sh ./tests/memory-qemu.sh

      - name: Real three-process native IPC service and M32 capability grants
        run: sh ./tests/ipc-qemu.sh
''',
    "- name: Real three-process native IPC service and M32 capability grants")
