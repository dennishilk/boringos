SHELL := /bin/sh

BUILD_DIR := build
KERNEL_BUILD_DIR := $(BUILD_DIR)/kernel
USER_BUILD_DIR := $(BUILD_DIR)/user
ISO_ROOT := $(BUILD_DIR)/iso_root
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ELF_SMOKE_OBJECT := $(USER_BUILD_DIR)/elf-smoke/start.o
ELF_SMOKE := $(USER_BUILD_DIR)/elf-smoke.elf
RUNTIME_ENTRY_OBJECT := $(USER_BUILD_DIR)/runtime/entry.o
RUNTIME_SYSCALL_OBJECT := $(USER_BUILD_DIR)/runtime/syscall.o
RUNTIME_MEMORY_OBJECT := $(USER_BUILD_DIR)/runtime/memory.o
RUNTIME_STRING_OBJECT := $(USER_BUILD_DIR)/runtime/string.o
IPC_RUNTIME_OBJECT := $(USER_BUILD_DIR)/runtime/ipc.o
DISPLAY_RUNTIME_OBJECT := $(USER_BUILD_DIR)/runtime/display.o
EVENT_RUNTIME_OBJECT := $(USER_BUILD_DIR)/runtime/event.o
DESKTOP_COMMON := $(RUNTIME_ENTRY_OBJECT) $(RUNTIME_SYSCALL_OBJECT) $(RUNTIME_MEMORY_OBJECT) $(RUNTIME_STRING_OBJECT) $(IPC_RUNTIME_OBJECT) $(EVENT_RUNTIME_OBJECT)
WM_ELF := $(USER_BUILD_DIR)/boringwm.elf
WM_DEATH_ELF := $(USER_BUILD_DIR)/boringwm-death.elf
DISPLAY_WM_ELF := $(USER_BUILD_DIR)/boring-display-wm.elf
WM_CLIENT_ELFS := $(USER_BUILD_DIR)/wm-client-a.elf $(USER_BUILD_DIR)/wm-client-b.elf $(USER_BUILD_DIR)/wm-client-c.elf
RUNTIME_COMMON_OBJECTS := $(RUNTIME_ENTRY_OBJECT) $(RUNTIME_SYSCALL_OBJECT) \
	$(RUNTIME_MEMORY_OBJECT) $(RUNTIME_STRING_OBJECT)
RUNTIME_MAIN_OBJECT := $(USER_BUILD_DIR)/runtime-smoke/main.o
RUNTIME_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(RUNTIME_MAIN_OBJECT)
RUNTIME_SMOKE := $(USER_BUILD_DIR)/runtime-smoke.elf
CONSOLE_MAIN_OBJECT := $(USER_BUILD_DIR)/console-smoke/main.o
CONSOLE_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(CONSOLE_MAIN_OBJECT)
CONSOLE_SMOKE := $(USER_BUILD_DIR)/console-smoke.elf
INIT_MAIN_OBJECT := $(USER_BUILD_DIR)/boring-init/main.o
INIT_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(INIT_MAIN_OBJECT)
INIT_ELF := $(USER_BUILD_DIR)/boring-init.elf
SHELL_INIT_MAIN_OBJECT := $(USER_BUILD_DIR)/boring-init-shell/main.o
SHELL_INIT_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(SHELL_INIT_MAIN_OBJECT)
SHELL_INIT_ELF := $(USER_BUILD_DIR)/boring-init-shell.elf
SHELL_MAIN_OBJECT := $(USER_BUILD_DIR)/boring-shell/main.o
SHELL_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(SHELL_MAIN_OBJECT)
SHELL_ELF := $(USER_BUILD_DIR)/boring-shell.elf
BORINGFETCH_MAIN_OBJECT := $(USER_BUILD_DIR)/boringfetch/main.o
BORINGFETCH_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(BORINGFETCH_MAIN_OBJECT)
BORINGFETCH_ELF := $(USER_BUILD_DIR)/boringfetch.elf
CAT_MAIN_OBJECT := $(USER_BUILD_DIR)/cat/main.o
CAT_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(CAT_MAIN_OBJECT)
CAT_ELF := $(USER_BUILD_DIR)/cat.elf
INPUT_TEST_MAIN_OBJECT := $(USER_BUILD_DIR)/input-test/main.o
INPUT_TEST_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(INPUT_TEST_MAIN_OBJECT)
INPUT_TEST_ELF := $(USER_BUILD_DIR)/input-test.elf
MEMORY_TEST_MAIN_OBJECT := $(USER_BUILD_DIR)/memory-test/main.o
MEMORY_TEST_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(MEMORY_TEST_MAIN_OBJECT)
MEMORY_TEST_ELF := $(USER_BUILD_DIR)/memory-test.elf
IPC_TEST_MAIN_OBJECT := $(USER_BUILD_DIR)/ipc-test/main.o
IPC_TEST_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(IPC_RUNTIME_OBJECT) $(IPC_TEST_MAIN_OBJECT)
IPC_TEST_ELF := $(USER_BUILD_DIR)/ipc-test.elf
M36_SPAWN_PARENT_MAIN_OBJECT := $(USER_BUILD_DIR)/m36-spawn-parent/main.o
M36_SPAWN_PARENT_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(M36_SPAWN_PARENT_MAIN_OBJECT)
M36_SPAWN_PARENT_ELF := $(USER_BUILD_DIR)/m36-spawn-parent.elf
M36_SPAWN_CHILD_MAIN_OBJECT := $(USER_BUILD_DIR)/m36-spawn-child/main.o
M36_SPAWN_CHILD_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(M36_SPAWN_CHILD_MAIN_OBJECT)
M36_SPAWN_CHILD_ELF := $(USER_BUILD_DIR)/m36-spawn-child.elf
BORING_DISPLAY_CORE_OBJECT := $(USER_BUILD_DIR)/boring-display/core.o
BORING_DISPLAY_MAIN_OBJECT := $(USER_BUILD_DIR)/boring-display/main.o
BORING_DISPLAY_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(IPC_RUNTIME_OBJECT) \
	$(DISPLAY_RUNTIME_OBJECT) $(BORING_DISPLAY_CORE_OBJECT) $(BORING_DISPLAY_MAIN_OBJECT)
BORING_DISPLAY_ELF := $(USER_BUILD_DIR)/boring-display.elf
DISPLAY_CLIENT_A_OBJECT := $(USER_BUILD_DIR)/display-client-a/main.o
DISPLAY_CLIENT_A_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(IPC_RUNTIME_OBJECT) $(DISPLAY_CLIENT_A_OBJECT)
DISPLAY_CLIENT_A_ELF := $(USER_BUILD_DIR)/display-client-a.elf
DISPLAY_CLIENT_B_OBJECT := $(USER_BUILD_DIR)/display-client-b/main.o
DISPLAY_CLIENT_B_OBJECTS := $(RUNTIME_COMMON_OBJECTS) $(IPC_RUNTIME_OBJECT) $(DISPLAY_CLIENT_B_OBJECT)
DISPLAY_CLIENT_B_ELF := $(USER_BUILD_DIR)/display-client-b.elf
ISO := $(BUILD_DIR)/boringos.iso
MKBORINGFS := $(BUILD_DIR)/mkboringfs
BORINGFSCK := $(BUILD_DIR)/boringfsck
BORINGFS_FIXTURE := $(BUILD_DIR)/boringfs-fixture
BORINGFS_VFS_HOST_TEST := $(BUILD_DIR)/boringfs-vfs-host-test
SHELL_HOST_TEST := $(BUILD_DIR)/shell-host-test
BORINGFETCH_HOST_TEST := $(BUILD_DIR)/boringfetch-host-test
FD_HOST_TEST := $(BUILD_DIR)/fd-host-test
PTY_HOST_TEST := $(BUILD_DIR)/pty-host-test
FRAMEBUFFER_HOST_TEST := $(BUILD_DIR)/framebuffer-host-test
INPUT_HOST_TEST := $(BUILD_DIR)/input-host-test
MEMORY_HOST_TEST := $(BUILD_DIR)/memory-host-test
RUNTIME_HEAP_HOST_TEST := $(BUILD_DIR)/runtime-heap-host-test
IPC_HOST_TEST := $(BUILD_DIR)/ipc-host-test
DISPLAY_HOST_TEST := $(BUILD_DIR)/display-host-test
PMM_READINESS_HOST_TEST := $(BUILD_DIR)/pmm-readiness-host-test
XHCI_HOST_TEST := $(BUILD_DIR)/xhci-host-test
MKBORINGFS_VERIFY := $(BUILD_DIR)/mkboringfs-test/mkboringfs-verify
BORINGFS_HEADER := libs/boringfs/include/boring/boringfs.h
BORINGFS_CODEC := libs/boringfs/codec.c
BORINGFS_VALIDATE := libs/boringfs/validate.c

CC = gcc
LD = ld
HOST_CC = cc
QEMU = qemu-system-x86_64
QEMU_CPU ?= qemu64,apic=off
CURL = curl
XORRISO = xorriso

HOST_CPPFLAGS := -Ilibs/boringfs/include
HOST_CFLAGS := -std=c11 -fno-builtin -fno-tree-loop-distribute-patterns \
	-Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes

BOOT_USER_ELF := $(ELF_SMOKE)
BOOT_USER_NAME := elf-smoke.elf
BOOT_EXTRA_USER_ELF :=
BOOT_EXTRA_USER_NAME :=
BOOT_EXTRA2_USER_ELF :=
BOOT_EXTRA2_USER_NAME :=
BOOT_EXTRA3_USER_ELF :=
BOOT_EXTRA4_USER_ELF :=
BOOT_LIMINE_CONF := limine.conf

TEST_MODE ?= normal
TEST_CPPFLAGS :=
ifeq ($(TEST_MODE),normal)
TEST_MODE_VALUE := 0
TEST_HARNESS_C := kernel/core/syscall_test.c
else ifeq ($(TEST_MODE),divide)
TEST_MODE_VALUE := 1
TEST_HARNESS_C := kernel/core/syscall_test.c
else ifeq ($(TEST_MODE),pagefault)
TEST_MODE_VALUE := 2
TEST_HARNESS_C := kernel/core/syscall_test.c
else ifeq ($(TEST_MODE),ring3)
TEST_MODE_VALUE := 3
TEST_HARNESS_C := kernel/core/syscall_test.c
else ifeq ($(TEST_MODE),syscall)
TEST_MODE_VALUE := 4
TEST_HARNESS_C := kernel/core/syscall_test.c
else ifeq ($(TEST_MODE),elf)
# Keep the established Milestone-11 acceptance isolated from the ordinary
# syscall acceptance while preserving its existing external test mode.
TEST_MODE_VALUE := 4
TEST_HARNESS_C := kernel/core/elf_test.c kernel/core/elf_test_adapter.c
else ifeq ($(TEST_MODE),runtime)
TEST_MODE_VALUE := 5
TEST_HARNESS_C := kernel/core/runtime_test.c kernel/core/runtime_test_adapter.c
BOOT_USER_ELF := $(RUNTIME_SMOKE)
BOOT_USER_NAME := runtime-smoke.elf
BOOT_LIMINE_CONF := limine-runtime.conf
else ifeq ($(TEST_MODE),console)
TEST_MODE_VALUE := 6
TEST_HARNESS_C := kernel/core/console_test.c kernel/core/console_test_adapter.c
BOOT_USER_ELF := $(CONSOLE_SMOKE)
BOOT_USER_NAME := console-smoke.elf
BOOT_LIMINE_CONF := limine-console.conf
else ifeq ($(TEST_MODE),vfs)
TEST_MODE_VALUE := 7
TEST_HARNESS_C := kernel/core/vfs_test.c
else ifeq ($(TEST_MODE),ramfs)
TEST_MODE_VALUE := 8
TEST_HARNESS_C := kernel/core/ramfs_test.c
else ifeq ($(TEST_MODE),init)
TEST_MODE_VALUE := 9
TEST_HARNESS_C := kernel/core/init_test.c
BOOT_USER_ELF := $(INIT_ELF)
BOOT_USER_NAME := boring-init.elf
BOOT_LIMINE_CONF := limine-init.conf
else ifeq ($(TEST_MODE),shell)
TEST_MODE_VALUE := 10
TEST_HARNESS_C := kernel/core/shell_test.c
BOOT_USER_ELF := $(SHELL_INIT_ELF)
BOOT_USER_NAME := boring-init.elf
BOOT_EXTRA_USER_ELF := $(SHELL_ELF)
BOOT_EXTRA_USER_NAME := boring-shell.elf
BOOT_LIMINE_CONF := limine-shell.conf
else ifeq ($(TEST_MODE),block)
TEST_MODE_VALUE := 11
TEST_HARNESS_C := kernel/core/block_device_test.c
else ifeq ($(TEST_MODE),virtio-block)
TEST_MODE_VALUE := 12
TEST_HARNESS_C := kernel/core/virtio_blk_test.c
else ifeq ($(TEST_MODE),boringfs-ro)
TEST_MODE_VALUE := 13
TEST_HARNESS_C := kernel/core/boringfs_ro_test.c
BOOT_USER_ELF := $(SHELL_INIT_ELF)
BOOT_USER_NAME := boring-init.elf
BOOT_EXTRA_USER_ELF := $(SHELL_ELF)
BOOT_EXTRA_USER_NAME := boring-shell.elf
BOOT_LIMINE_CONF := limine-shell.conf
else ifeq ($(TEST_MODE),boringfs-rw)
TEST_MODE_VALUE := 14
TEST_HARNESS_C := kernel/core/boringfs_ro_test.c
BOOT_USER_ELF := $(SHELL_INIT_ELF)
BOOT_USER_NAME := boring-init.elf
BOOT_EXTRA_USER_ELF := $(SHELL_ELF)
BOOT_EXTRA_USER_NAME := boring-shell.elf
BOOT_LIMINE_CONF := limine-shell.conf
else ifeq ($(TEST_MODE),persistent-root)
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
else ifeq ($(TEST_MODE),m36-spawn)
TEST_MODE_VALUE := 5
TEST_HARNESS_C := kernel/core/m36_spawn_test.c kernel/core/m36_spawn_test_adapter.c
BOOT_USER_ELF := $(M36_SPAWN_PARENT_ELF)
BOOT_USER_NAME := m36-spawn-parent.elf
BOOT_LIMINE_CONF := limine-m36-spawn.conf
else ifeq ($(TEST_MODE),m36-desktop)
TEST_MODE_VALUE := 5
TEST_CPPFLAGS := -DBORING_M36_DESKTOP_ACCEPTANCE=1
TEST_HARNESS_C := kernel/core/m36_desktop_test.c kernel/core/m36_desktop_test_adapter.c
BOOT_USER_ELF := $(DISPLAY_WM_ELF)
BOOT_USER_NAME := boring-display.elf
BOOT_EXTRA_USER_ELF := $(WM_ELF)
BOOT_EXTRA_USER_NAME := boringwm.elf
BOOT_LIMINE_CONF := limine-m36-desktop.conf
else ifeq ($(TEST_MODE),m48-xhci)
TEST_MODE_VALUE := 5
TEST_HARNESS_C := kernel/core/xhci_test.c kernel/core/xhci_test_adapter.c
else ifeq ($(TEST_MODE),m34-display)
# Reuse the established special-test entry seam (value 5); the adapter below
# routes it to the dedicated three-process display_test_run() harness.
TEST_MODE_VALUE := 5
TEST_HARNESS_C := kernel/core/display_test.c kernel/core/display_test_adapter.c
BOOT_USER_ELF := $(BORING_DISPLAY_ELF)
BOOT_USER_NAME := boring-display.elf
BOOT_EXTRA_USER_ELF := $(DISPLAY_CLIENT_A_ELF)
BOOT_EXTRA_USER_NAME := display-client-a.elf
BOOT_EXTRA2_USER_ELF := $(DISPLAY_CLIENT_B_ELF)
BOOT_EXTRA2_USER_NAME := display-client-b.elf
BOOT_LIMINE_CONF := limine-display.conf
else ifneq ($(filter $(TEST_MODE),m35-wm m35-wm-death),)
TEST_MODE_VALUE := 5
TEST_HARNESS_C := kernel/core/wm_test.c kernel/core/wm_test_adapter.c
BOOT_USER_ELF := $(DISPLAY_WM_ELF)
BOOT_USER_NAME := boring-display.elf
BOOT_EXTRA_USER_ELF := $(WM_ELF)
ifeq ($(TEST_MODE),m35-wm-death)
BOOT_EXTRA_USER_ELF := $(WM_DEATH_ELF)
endif
BOOT_EXTRA_USER_NAME := boringwm.elf
BOOT_EXTRA2_USER_ELF := $(USER_BUILD_DIR)/wm-client-a.elf
BOOT_EXTRA2_USER_NAME := wm-client-a.elf
BOOT_EXTRA3_USER_ELF := $(USER_BUILD_DIR)/wm-client-b.elf
BOOT_EXTRA4_USER_ELF := $(USER_BUILD_DIR)/wm-client-c.elf
BOOT_LIMINE_CONF := limine-wm.conf
else
$(error unsupported TEST_MODE '$(TEST_MODE)'; use normal, divide, pagefault, ring3, syscall, elf, runtime, console, vfs, ramfs, init, shell, block, virtio-block, boringfs-ro, boringfs-rw, persistent-root, m33-ipc, m34-display, m35-wm, m35-wm-death, m36-spawn, m36-desktop, or m48-xhci)
endif

LIMINE_VERSION := 12.5.2
LIMINE_ARCHIVE := $(BUILD_DIR)/deps/limine-binary.tar.gz
LIMINE_DIR := $(BUILD_DIR)/deps/limine-binary
LIMINE_SHA256 := 4c760c09c53560d859b362319a3dc63b79cca3d47f35d69ab0106a13b8057055
LIMINE_URL := https://github.com/Limine-Bootloader/Limine/releases/download/v$(LIMINE_VERSION)/limine-binary.tar.gz

CPPFLAGS := -Ikernel/include -Ilibs/boringfs/include -DBORING_TEST_MODE=$(TEST_MODE_VALUE) $(TEST_CPPFLAGS)
CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-m64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
	-mno-red-zone -mcmodel=kernel -O2 -g \
	-Wall -Wextra -Wpedantic -Werror \
	-Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
ASFLAGS := -ffreestanding -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker/x86_64.ld
ELF_USER_CPPFLAGS := -Ikernel/include
USER_ASFLAGS := -ffreestanding -fno-pic -fno-pie -m64 -mno-red-zone
USER_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/elf-smoke/linker.ld
RUNTIME_USER_CPPFLAGS := -Iuser/runtime/include -Ikernel/include
RUNTIME_USER_CFLAGS := -std=c11 -ffreestanding -fno-stack-protector \
	-fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -m64 -mno-red-zone -mno-80387 -mno-mmx \
	-mno-sse -mno-sse2 -O2 -Wall -Wextra -Wpedantic -Werror \
	-Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
RUNTIME_USER_ASFLAGS := -ffreestanding -fno-pic -fno-pie -m64 -mno-red-zone
RUNTIME_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/runtime-smoke/linker.ld
INIT_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/boring-init/linker.ld
SHELL_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/boring-shell/linker.ld
BORINGFETCH_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/boringfetch/linker.ld
CAT_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/cat/linker.ld
INPUT_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/input-test/linker.ld
MEMORY_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/memory-test/linker.ld
IPC_TEST_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/memory-test/linker.ld
DISPLAY_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000 \
	-T user/memory-test/linker.ld

KERNEL_C_SOURCES := \
	kernel/core/smbios.c \
	kernel/core/smbios_limine.c \
	kernel/core/pci_inventory.c \
	kernel/core/pci_inventory_x86.c \
	kernel/core/xhci.c \
	kernel/core/usb_hid.c \
	kernel/core/cpu_inventory.c \
	kernel/core/cpu_inventory_x86.c \
	kernel/core/entry.c \
	kernel/core/framebuffer.c \
	kernel/core/framebuffer_user.c \
	kernel/core/graphics.c \
	kernel/core/pixel_font.c \
	kernel/core/boot_dashboard.c \
	kernel/core/pmm.c \
	kernel/core/heap.c \
	kernel/core/process.c \
	kernel/core/fd.c \
	kernel/core/pty.c \
	kernel/core/input.c \
	kernel/core/user_memory.c \
	kernel/core/ipc.c \
	kernel/core/ipc_syscall.c \
	kernel/core/display_syscall.c \
	kernel/core/event_syscall.c \
	kernel/core/display_test_stub.c \
	kernel/core/ipc_test.c \
	kernel/core/vfs.c \
	kernel/core/ramfs.c \
	kernel/core/block_device.c \
	kernel/fs/boringfs_vfs.c \
	libs/boringfs/codec.c \
	libs/boringfs/validate.c \
	kernel/drivers/virtio_blk.c \
	kernel/core/task.c \
	kernel/core/preemption_test.c \
	kernel/core/process_test.c \
	kernel/core/ring3_test.c \
	kernel/core/syscall.c \
	kernel/core/m36_syscall.c \
	kernel/core/spawn_stack.c \
	kernel/core/elf_boot.c \
	kernel/core/elf_loader.c \
	kernel/core/elf_vfs.c \
	$(TEST_HARNESS_C) \
	kernel/arch/x86_64/vmm.c \
	kernel/arch/x86_64/mmio.c \
	kernel/arch/x86_64/pci.c \
	kernel/arch/x86_64/xhci.c \
	kernel/arch/x86_64/address_space.c \
	kernel/arch/x86_64/ring3_memory.c \
	kernel/arch/x86_64/descriptor.c \
	kernel/arch/x86_64/exception.c \
	kernel/arch/x86_64/irq.c \
	kernel/arch/x86_64/i8042.c \
	kernel/arch/x86_64/ps2_keyboard.c \
	kernel/arch/x86_64/ps2_mouse.c \
	kernel/arch/x86_64/timer.c \
	kernel/arch/x86_64/serial.c \
	kernel/arch/x86_64/cpu.c
KERNEL_ASM_SOURCES := \
	kernel/arch/x86_64/descriptor_stubs.S \
	kernel/arch/x86_64/exception_stubs.S \
	kernel/arch/x86_64/irq_stubs.S \
	kernel/arch/x86_64/context_switch.S \
	kernel/arch/x86_64/ring3_entry.S \
	kernel/arch/x86_64/syscall_entry.S \
	kernel/arch/x86_64/syscall_test_payload.S
KERNEL_C_OBJECTS := $(patsubst %.c,$(KERNEL_BUILD_DIR)/%.o,$(KERNEL_C_SOURCES))
KERNEL_ASM_OBJECTS := $(patsubst %.S,$(KERNEL_BUILD_DIR)/%.o,$(KERNEL_ASM_SOURCES))
KERNEL_OBJECTS := $(KERNEL_C_OBJECTS) $(KERNEL_ASM_OBJECTS)
MODE_STAMP := $(BUILD_DIR)/.test-mode

.PHONY: all kernel user-elf user-runtime user-console user-init user-shell user-boringfetch user-cat user-input-test user-memory-test user-ipc-test user-m36-spawn user-boring-display user-display-clients elf-audit runtime-audit console-audit init-audit shell-audit boringfetch-audit cat-audit input-test-audit memory-test-audit ipc-test-audit display-audit shell-host-test fd-host-test pty-host-test framebuffer-host-test input-host-test memory-host-test ipc-host-test display-host-test boringfs-host-test boringfs-vfs-host-test mkboringfs mkboringfs-test boringfsck boringfsck-test boringfs-fixture qemu-bundle run run-headless test clean distclean

all: $(ISO)

kernel: $(KERNEL_ELF)

user-elf: $(ELF_SMOKE)

user-runtime: $(RUNTIME_SMOKE)

user-console: $(CONSOLE_SMOKE)

user-init: $(INIT_ELF)

user-shell: $(SHELL_INIT_ELF) $(SHELL_ELF)

user-boringfetch: $(BORINGFETCH_ELF)

user-cat: $(CAT_ELF)

user-input-test: $(INPUT_TEST_ELF)

user-memory-test: $(MEMORY_TEST_ELF)

user-ipc-test: $(IPC_TEST_ELF)

user-m36-spawn: $(M36_SPAWN_PARENT_ELF) $(M36_SPAWN_CHILD_ELF)

user-boring-display: $(BORING_DISPLAY_ELF)

user-display-clients: $(DISPLAY_CLIENT_A_ELF) $(DISPLAY_CLIENT_B_ELF)

elf-audit: $(ELF_SMOKE)
	sh ./tests/elf-build-audit.sh

runtime-audit: $(RUNTIME_SMOKE)
	sh ./tests/runtime-build-audit.sh

console-audit: $(CONSOLE_SMOKE)
	sh ./tests/console-build-audit.sh

init-audit: $(INIT_ELF)
	sh ./tests/init-build-audit.sh

shell-audit: $(SHELL_ELF)
	sh ./tests/shell-build-audit.sh

boringfetch-audit: $(BORINGFETCH_ELF)
	sh ./tests/boringfetch-build-audit.sh

cat-audit: $(CAT_ELF)
	sh ./tests/cat-build-audit.sh

input-test-audit: $(INPUT_TEST_ELF)
	sh ./tests/input-test-build-audit.sh

memory-test-audit: $(MEMORY_TEST_ELF)
	sh ./tests/memory-test-build-audit.sh

ipc-test-audit: $(IPC_TEST_ELF)
	sh ./tests/ipc-test-build-audit.sh

display-audit: $(BORING_DISPLAY_ELF) $(DISPLAY_CLIENT_A_ELF) $(DISPLAY_CLIENT_B_ELF)
	sh ./tests/display-build-audit.sh

shell-host-test: $(SHELL_HOST_TEST)
	$(SHELL_HOST_TEST)

.PHONY: boringfetch-host-test
boringfetch-host-test: $(BORINGFETCH_HOST_TEST)
	$(BORINGFETCH_HOST_TEST)

fd-host-test: $(FD_HOST_TEST)
	$(FD_HOST_TEST)

pty-host-test: $(PTY_HOST_TEST)
	$(PTY_HOST_TEST)

framebuffer-host-test: $(FRAMEBUFFER_HOST_TEST)
	$(FRAMEBUFFER_HOST_TEST)

input-host-test: $(INPUT_HOST_TEST)
	$(INPUT_HOST_TEST)

memory-host-test: $(MEMORY_HOST_TEST) $(RUNTIME_HEAP_HOST_TEST)
	$(MEMORY_HOST_TEST)
	$(RUNTIME_HEAP_HOST_TEST)

ipc-host-test: $(IPC_HOST_TEST)
	$(IPC_HOST_TEST)

display-host-test: $(DISPLAY_HOST_TEST)
	$(DISPLAY_HOST_TEST)

.PHONY: pmm-readiness-host-test
pmm-readiness-host-test: $(PMM_READINESS_HOST_TEST)
	$(PMM_READINESS_HOST_TEST)

.PHONY: xhci-host-test
xhci-host-test: $(XHCI_HOST_TEST)
	$(XHCI_HOST_TEST)

boringfs-host-test:
	sh ./tests/boringfs-host-test.sh

boringfs-vfs-host-test: $(BORINGFS_VFS_HOST_TEST) $(BORINGFS_FIXTURE)
	$(BORINGFS_FIXTURE) $(BUILD_DIR)/boringfs-vfs-host-test.raw valid
	$(BORINGFS_VFS_HOST_TEST) $(BUILD_DIR)/boringfs-vfs-host-test.raw

mkboringfs: $(MKBORINGFS)

mkboringfs-test: $(MKBORINGFS) $(MKBORINGFS_VERIFY)
	sh ./tests/mkboringfs-test.sh

boringfsck: $(BORINGFSCK)

boringfs-fixture: $(BORINGFS_FIXTURE)

qemu-bundle:
	$(MAKE) TEST_MODE=persistent-root
	$(MAKE) boringfs-fixture
	$(MAKE) user-boringfetch
	$(MAKE) user-cat
	$(MAKE) user-input-test
	$(MAKE) user-memory-test
	$(MAKE) user-ipc-test
	mkdir -p $(BUILD_DIR)/boringos-qemu-x86_64
	cp $(ISO) $(BUILD_DIR)/boringos-qemu-x86_64/boringos.iso
	$(BORINGFS_FIXTURE) $(BUILD_DIR)/boringos-qemu-x86_64/boringos-root.img valid $(BORINGFETCH_ELF) $(CAT_ELF) $(INPUT_TEST_ELF) $(MEMORY_TEST_ELF) $(IPC_TEST_ELF)
	cp scripts/run-boringos.sh $(BUILD_DIR)/boringos-qemu-x86_64/run-boringos.sh
	cp docs/RUNNING.md $(BUILD_DIR)/boringos-qemu-x86_64/README.md
	cd $(BUILD_DIR)/boringos-qemu-x86_64 && sha256sum boringos.iso boringos-root.img > SHA256SUMS

boringfsck-test: $(MKBORINGFS) $(BORINGFSCK)
	sh ./tests/boringfsck-test.sh

$(MKBORINGFS): tools/mkboringfs.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) $(BORINGFS_HEADER)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) \
		tools/mkboringfs.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) -o $@

$(BORINGFSCK): tools/boringfsck.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) $(BORINGFS_HEADER)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) \
		tools/boringfsck.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) -o $@

$(BORINGFS_FIXTURE): tests/boringfs-fixture.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) $(BORINGFS_HEADER)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) \
		tests/boringfs-fixture.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) -o $@

$(BORINGFS_VFS_HOST_TEST): tests/boringfs-vfs-host-test.c \
		kernel/fs/boringfs_vfs.c kernel/core/block_device.c \
		kernel/include/boring/boringfs_vfs.h kernel/include/boring/block_device.h \
		kernel/include/boring/vfs.h kernel/include/boring/heap.h \
		$(BORINGFS_CODEC) $(BORINGFS_VALIDATE) $(BORINGFS_HEADER)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) -Ikernel/include $(HOST_CFLAGS) \
		tests/boringfs-vfs-host-test.c kernel/fs/boringfs_vfs.c \
		kernel/core/block_device.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) -o $@

$(SHELL_HOST_TEST): tests/shell-host-test.c user/boring-shell/main.c \
		user/runtime/include/boring/runtime.h \
		user/runtime/include/boring/syscall.h \
		user/runtime/include/boring/string.h \
		kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Iuser/runtime/include -Ikernel/include $(HOST_CFLAGS) \
		tests/shell-host-test.c -o $@

$(BORINGFETCH_HOST_TEST): tests/boringfetch-host-test.c \
		user/boringfetch/main.c user/runtime/string.c \
		user/runtime/include/boring/syscall.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Iuser/runtime/include -Ikernel/include $(HOST_CFLAGS) \
		tests/boringfetch-host-test.c user/boringfetch/main.c \
		user/runtime/string.c -o $@

$(FD_HOST_TEST): tests/fd-host-test.c kernel/core/fd.c kernel/core/pty.c \
		kernel/include/boring/fd.h kernel/include/boring/vfs.h \
		kernel/include/boring/pty.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/fd-host-test.c kernel/core/fd.c kernel/core/pty.c -o $@

$(PTY_HOST_TEST): tests/pty-host-test.c kernel/core/pty.c \
		kernel/include/boring/pty.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/pty-host-test.c kernel/core/pty.c -o $@

$(FRAMEBUFFER_HOST_TEST): tests/framebuffer-host-test.c \
		kernel/core/framebuffer.c kernel/core/graphics.c kernel/core/pixel_font.c \
		kernel/include/boring/framebuffer.h kernel/include/boring/graphics.h \
		kernel/include/boring/pixel_font.h kernel/include/boring/boot_protocol.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/framebuffer-host-test.c kernel/core/framebuffer.c \
		kernel/core/graphics.c kernel/core/pixel_font.c -o $@

$(INPUT_HOST_TEST): tests/input-host-test.c kernel/core/input.c \
		kernel/arch/x86_64/ps2_keyboard.c kernel/arch/x86_64/ps2_mouse.c \
		kernel/include/boring/input.h kernel/include/boring/input_abi.h \
		kernel/include/boring/ps2_keyboard.h kernel/include/boring/ps2_mouse.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/input-host-test.c kernel/core/input.c \
		kernel/arch/x86_64/ps2_keyboard.c kernel/arch/x86_64/ps2_mouse.c -o $@

$(MEMORY_HOST_TEST): tests/memory-host-test.c kernel/core/user_memory.c \
		kernel/include/boring/user_memory.h kernel/include/boring/process.h \
		kernel/include/boring/ring3_memory.h kernel/include/boring/address_space.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/memory-host-test.c kernel/core/user_memory.c -o $@

$(IPC_HOST_TEST): tests/ipc-host-test.c kernel/core/ipc.c \
		kernel/include/boring/ipc.h kernel/include/boring/process.h \
		kernel/include/boring/task.h kernel/include/boring/user_memory.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/ipc-host-test.c kernel/core/ipc.c -o $@

$(DISPLAY_HOST_TEST): tests/display-host-test.c user/boring-display/core.c \
		user/boring-display/core.h kernel/include/boring/display_abi.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/display-host-test.c user/boring-display/core.c -o $@

$(PMM_READINESS_HOST_TEST): tests/pmm-readiness-host-test.c \
		kernel/core/pmm.c kernel/include/boring/pmm.h \
		kernel/include/boring/boot_protocol.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) \
		tests/pmm-readiness-host-test.c kernel/core/pmm.c -o $@

$(RUNTIME_HEAP_HOST_TEST): tests/runtime-heap-host-test.c user/runtime/memory.c \
		user/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h \
		kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Iuser/runtime/include -Ikernel/include $(HOST_CFLAGS) \
		tests/runtime-heap-host-test.c user/runtime/memory.c -o $@

$(MKBORINGFS_VERIFY): tests/mkboringfs-verify.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) $(BORINGFS_HEADER)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) \
		tests/mkboringfs-verify.c $(BORINGFS_CODEC) $(BORINGFS_VALIDATE) -o $@

.PHONY: check-test-mode
check-test-mode:

$(MODE_STAMP): check-test-mode
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' '$(TEST_MODE)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(KERNEL_BUILD_DIR)/%.o: %.c $(MODE_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(KERNEL_BUILD_DIR)/%.o: %.S $(MODE_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJECTS) kernel/linker/x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $@

$(ELF_SMOKE_OBJECT): user/elf-smoke/start.S kernel/include/boring/elf_smoke.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(ELF_USER_CPPFLAGS) $(USER_ASFLAGS) -c $< -o $@

$(ELF_SMOKE): $(ELF_SMOKE_OBJECT) user/elf-smoke/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) $(ELF_SMOKE_OBJECT) -o $@

$(RUNTIME_ENTRY_OBJECT): user/runtime/entry.S
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_ASFLAGS) -c $< -o $@

$(RUNTIME_SYSCALL_OBJECT): user/runtime/syscall.c user/runtime/include/boring/syscall.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(RUNTIME_MEMORY_OBJECT): user/runtime/memory.c user/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(RUNTIME_STRING_OBJECT): user/runtime/string.c user/runtime/include/boring/string.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(IPC_RUNTIME_OBJECT): user/runtime/ipc.c user/runtime/include/boring/ipc.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(DISPLAY_RUNTIME_OBJECT): user/runtime/display.c user/runtime/include/boring/display.h kernel/include/boring/display_abi.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(RUNTIME_MAIN_OBJECT): user/runtime-smoke/main.c \
	user/runtime/include/boring/runtime.h \
	user/runtime/include/boring/syscall.h \
	user/runtime/include/boring/memory.h \
	user/runtime/include/boring/string.h \
	kernel/include/boring/runtime_smoke.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(RUNTIME_SMOKE): $(RUNTIME_OBJECTS) user/runtime-smoke/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(RUNTIME_LDFLAGS) $(RUNTIME_OBJECTS) -o $@

$(CONSOLE_MAIN_OBJECT): user/console-smoke/main.c \
	user/runtime/include/boring/runtime.h \
	user/runtime/include/boring/syscall.h \
	user/runtime/include/boring/memory.h \
	user/runtime/include/boring/string.h \
	kernel/include/boring/console_smoke.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(CONSOLE_SMOKE): $(CONSOLE_OBJECTS) user/runtime-smoke/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(RUNTIME_LDFLAGS) $(CONSOLE_OBJECTS) -o $@

$(INIT_MAIN_OBJECT): user/boring-init/main.c \
	user/runtime/include/boring/runtime.h \
	user/runtime/include/boring/syscall.h \
	kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(INIT_ELF): $(INIT_OBJECTS) user/boring-init/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(INIT_LDFLAGS) $(INIT_OBJECTS) -o $@

$(SHELL_INIT_MAIN_OBJECT): user/boring-init/main.c \
	user/runtime/include/boring/runtime.h \
	user/runtime/include/boring/syscall.h \
	kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) \
		-DBORING_INIT_LAUNCH_SHELL=1 -c $< -o $@

$(SHELL_INIT_ELF): $(SHELL_INIT_OBJECTS) user/boring-init/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(INIT_LDFLAGS) $(SHELL_INIT_OBJECTS) -o $@

$(SHELL_MAIN_OBJECT): user/boring-shell/main.c \
	user/runtime/include/boring/runtime.h \
	user/runtime/include/boring/syscall.h \
	user/runtime/include/boring/string.h \
	kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(SHELL_ELF): $(SHELL_OBJECTS) user/boring-shell/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(SHELL_LDFLAGS) $(SHELL_OBJECTS) -o $@

$(BORINGFETCH_MAIN_OBJECT): user/boringfetch/main.c user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(BORINGFETCH_ELF): $(BORINGFETCH_OBJECTS) user/boringfetch/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(BORINGFETCH_LDFLAGS) $(BORINGFETCH_OBJECTS) -o $@

$(CAT_MAIN_OBJECT): user/cat/main.c user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(CAT_ELF): $(CAT_OBJECTS) user/cat/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(CAT_LDFLAGS) $(CAT_OBJECTS) -o $@

$(INPUT_TEST_MAIN_OBJECT): user/input-test/main.c user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h kernel/include/boring/input_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(INPUT_TEST_ELF): $(INPUT_TEST_OBJECTS) user/input-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(INPUT_TEST_LDFLAGS) $(INPUT_TEST_OBJECTS) -o $@

$(MEMORY_TEST_MAIN_OBJECT): user/memory-test/main.c user/runtime/include/boring/memory.h user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(MEMORY_TEST_ELF): $(MEMORY_TEST_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(MEMORY_TEST_LDFLAGS) $(MEMORY_TEST_OBJECTS) -o $@

$(IPC_TEST_MAIN_OBJECT): user/ipc-test/main.c user/runtime/include/boring/ipc.h user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(IPC_TEST_ELF): $(IPC_TEST_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(IPC_TEST_LDFLAGS) $(IPC_TEST_OBJECTS) -o $@

$(M36_SPAWN_PARENT_MAIN_OBJECT): user/m36-spawn-parent/main.c user/runtime/include/boring/syscall.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(M36_SPAWN_PARENT_ELF): $(M36_SPAWN_PARENT_OBJECTS) user/m36-spawn-parent/linker.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -static --build-id=none -z max-page-size=0x1000 -T user/m36-spawn-parent/linker.ld $(M36_SPAWN_PARENT_OBJECTS) -o $@

$(M36_SPAWN_CHILD_MAIN_OBJECT): user/m36-spawn-child/main.c user/runtime/include/boring/syscall.h user/runtime/include/boring/string.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(M36_SPAWN_CHILD_ELF): $(M36_SPAWN_CHILD_OBJECTS) user/m36-spawn-child/linker.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -static --build-id=none -z max-page-size=0x1000 -T user/m36-spawn-child/linker.ld $(M36_SPAWN_CHILD_OBJECTS) -o $@

$(BORING_DISPLAY_CORE_OBJECT): user/boring-display/core.c user/boring-display/core.h kernel/include/boring/display_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(BORING_DISPLAY_MAIN_OBJECT): user/boring-display/main.c user/boring-display/core.h \
		user/runtime/include/boring/display.h user/runtime/include/boring/ipc.h \
		user/runtime/include/boring/syscall.h kernel/include/boring/display_abi.h \
		kernel/include/boring/input_abi.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(BORING_DISPLAY_ELF): $(BORING_DISPLAY_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(DISPLAY_LDFLAGS) $(BORING_DISPLAY_OBJECTS) -o $@

$(DISPLAY_CLIENT_A_OBJECT): user/display-client-a/main.c \
		user/runtime/include/boring/ipc.h user/runtime/include/boring/syscall.h \
		kernel/include/boring/display_abi.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(DISPLAY_CLIENT_A_ELF): $(DISPLAY_CLIENT_A_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(DISPLAY_LDFLAGS) $(DISPLAY_CLIENT_A_OBJECTS) -o $@

$(DISPLAY_CLIENT_B_OBJECT): user/display-client-b/main.c \
		user/runtime/include/boring/ipc.h user/runtime/include/boring/syscall.h \
		kernel/include/boring/display_abi.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(DISPLAY_CLIENT_B_ELF): $(DISPLAY_CLIENT_B_OBJECTS) user/memory-test/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(DISPLAY_LDFLAGS) $(DISPLAY_CLIENT_B_OBJECTS) -o $@

$(LIMINE_ARCHIVE):
	@mkdir -p $(dir $@)
	$(CURL) --fail --location --retry 3 --output $@ $(LIMINE_URL)
	printf '%s  %s\n' '$(LIMINE_SHA256)' '$@' | sha256sum -c -

$(LIMINE_DIR)/limine: $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	tar -xzf $(LIMINE_ARCHIVE) -C $(BUILD_DIR)/deps
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)"

$(ISO): $(KERNEL_ELF) $(BOOT_USER_ELF) $(BOOT_EXTRA_USER_ELF) $(BOOT_EXTRA2_USER_ELF) $(BOOT_EXTRA3_USER_ELF) $(BOOT_EXTRA4_USER_ELF) $(LIMINE_DIR)/limine $(BOOT_LIMINE_CONF)
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/boot/user $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp $(BOOT_USER_ELF) $(ISO_ROOT)/boot/user/$(BOOT_USER_NAME)
	@if [ -n "$(BOOT_EXTRA_USER_ELF)" ]; then \
		cp $(BOOT_EXTRA_USER_ELF) $(ISO_ROOT)/boot/user/$(BOOT_EXTRA_USER_NAME); \
	fi
	@if [ -n "$(BOOT_EXTRA2_USER_ELF)" ]; then \
		cp $(BOOT_EXTRA2_USER_ELF) $(ISO_ROOT)/boot/user/$(BOOT_EXTRA2_USER_NAME); \
	fi
	cp $(BOOT_LIMINE_CONF) $(ISO_ROOT)/boot/limine/limine.conf
	@if [ -n "$(BOOT_EXTRA3_USER_ELF)" ]; then cp $(BOOT_EXTRA3_USER_ELF) $(ISO_ROOT)/boot/user/wm-client-b.elf; fi
	@if [ -n "$(BOOT_EXTRA4_USER_ELF)" ]; then cp $(BOOT_EXTRA4_USER_ELF) $(ISO_ROOT)/boot/user/wm-client-c.elf; fi
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO)
	$(LIMINE_DIR)/limine bios-install $(ISO)
	rm -rf $(ISO_ROOT)

run: $(ISO)
	$(QEMU) -M q35 -cpu "$(QEMU_CPU)" -m 128M -cdrom $(ISO) -boot d \
		-display none -serial stdio -monitor none -no-reboot -no-shutdown

run-headless: run

test:
	sh ./tests/elf-build-audit.sh
	sh ./tests/runtime-build-audit.sh
	sh ./tests/console-build-audit.sh
	sh ./tests/init-build-audit.sh
	./tests/boot-qemu.sh
	./tests/exception-divide-qemu.sh
	./tests/exception-pagefault-qemu.sh
	./tests/ring3-qemu.sh
	./tests/syscall-qemu.sh
	sh ./tests/elf-qemu.sh
	sh ./tests/runtime-qemu.sh
	sh ./tests/console-qemu.sh
	sh ./tests/vfs-qemu.sh
	sh ./tests/ramfs-qemu.sh
	sh ./tests/init-qemu.sh
	sh ./tests/shell-build-audit.sh
	sh ./tests/boringfetch-build-audit.sh
	sh ./tests/cat-build-audit.sh
	sh ./tests/input-test-build-audit.sh
	sh ./tests/memory-test-build-audit.sh
	sh ./tests/ipc-test-build-audit.sh
	sh ./tests/display-build-audit.sh
	$(MAKE) shell-host-test
	$(MAKE) fd-host-test
	$(MAKE) pty-host-test
	$(MAKE) framebuffer-host-test
	$(MAKE) input-host-test
	$(MAKE) memory-host-test
	$(MAKE) ipc-host-test
	$(MAKE) display-host-test
	sh ./tests/shell-qemu.sh
	sh ./tests/shell-editing-qemu.sh
	sh ./tests/shell-lifecycle-qemu.sh
	sh ./tests/shell-input-stress-qemu.sh
	sh ./tests/block-device-qemu.sh
	sh ./tests/virtio-block-qemu.sh
	sh ./tests/boringfs-ro-qemu.sh
	sh ./tests/boringfs-rw-qemu.sh
	sh ./tests/persistent-root-qemu.sh
	sh ./tests/fd-stdio-qemu.sh
	sh ./tests/input-qemu.sh
	sh ./tests/memory-qemu.sh
	sh ./tests/ipc-qemu.sh
	sh ./tests/framebuffer-qemu.sh
	sh ./tests/display-qemu.sh
	sh ./tests/boringfs-host-test.sh
	$(MAKE) boringfs-vfs-host-test
	$(MAKE) mkboringfs-test
	$(MAKE) boringfsck-test

clean:
	rm -rf $(BUILD_DIR)

distclean: clean

# M35 is additive: the M34 service/client binaries and tests remain intact.
.PHONY: user-boringwm wm-host-test wm-audit
user-boringwm: $(WM_ELF) $(WM_DEATH_ELF) $(DISPLAY_WM_ELF) $(WM_CLIENT_ELFS)

$(USER_BUILD_DIR)/runtime/event.o: user/runtime/event.c user/runtime/include/boring/event.h kernel/include/boring/event_abi.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/boringwm/%.o: user/boringwm/%.c user/boringwm/core.h user/runtime/include/boring/wm.h user/runtime/include/boring/display_control.h user/runtime/include/boring/desktop_log.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/boringwm/death.o: user/boringwm/main.c user/boringwm/core.h user/runtime/include/boring/wm.h user/runtime/include/boring/display_control.h user/runtime/include/boring/desktop_log.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -DBORING_WM_DEATH_ACCEPTANCE -c $< -o $@

$(USER_BUILD_DIR)/boring-display/%.o: user/boring-display/%.c user/boring-display/managed.h user/boring-display/core.h user/runtime/include/boring/display_control.h user/runtime/include/boring/desktop_log.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(WM_ELF): $(DESKTOP_COMMON) $(USER_BUILD_DIR)/boringwm/core.o $(USER_BUILD_DIR)/boringwm/main.o
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@

$(WM_DEATH_ELF): $(DESKTOP_COMMON) $(USER_BUILD_DIR)/boringwm/core.o $(USER_BUILD_DIR)/boringwm/death.o
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@

$(DISPLAY_WM_ELF): $(DESKTOP_COMMON) $(DISPLAY_RUNTIME_OBJECT) $(BORING_DISPLAY_CORE_OBJECT) $(USER_BUILD_DIR)/boring-display/managed.o $(USER_BUILD_DIR)/boring-display/server.o
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@

$(USER_BUILD_DIR)/wm-client-a.o: user/wm-client/main.c user/runtime/include/boring/wm.h user/runtime/include/boring/display_control.h user/runtime/include/boring/desktop_log.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -DWM_CLIENT_ID=1 -c $< -o $@
$(USER_BUILD_DIR)/wm-client-b.o: user/wm-client/main.c user/runtime/include/boring/wm.h user/runtime/include/boring/display_control.h user/runtime/include/boring/desktop_log.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -DWM_CLIENT_ID=2 -c $< -o $@
$(USER_BUILD_DIR)/wm-client-c.o: user/wm-client/main.c user/runtime/include/boring/wm.h user/runtime/include/boring/display_control.h user/runtime/include/boring/desktop_log.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -DWM_CLIENT_ID=3 -c $< -o $@
$(USER_BUILD_DIR)/wm-client-%.elf: $(USER_BUILD_DIR)/wm-client-%.o $(DESKTOP_COMMON) $(DISPLAY_RUNTIME_OBJECT)
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@

$(BUILD_DIR)/wm-host-test: tests/wm-host-test.c user/boringwm/core.c user/boringwm/core.h user/boring-display/managed.c user/boring-display/managed.h user/boring-display/core.c user/boring-display/core.h user/runtime/include/boring/wm.h user/runtime/include/boring/display_control.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(RUNTIME_USER_CPPFLAGS) $(HOST_CFLAGS) tests/wm-host-test.c user/boringwm/core.c user/boring-display/managed.c user/boring-display/core.c -o $@
wm-host-test: $(BUILD_DIR)/wm-host-test
	$(BUILD_DIR)/wm-host-test
wm-audit: user-boringwm
	sh tests/wm-build-audit.sh

# M42 shared native desktop client lifecycle.
CLIENT_RUNTIME_OBJECT := $(USER_BUILD_DIR)/runtime/client.o
$(CLIENT_RUNTIME_OBJECT): user/runtime/client.c user/runtime/include/boring/client.h user/runtime/include/boring/wm.h user/runtime/include/boring/display_control.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

# M36 native managed terminal client and deterministic parser/renderer.
TERMINAL_MAIN_OBJECT := $(USER_BUILD_DIR)/boring-terminal/main.o
TERMINAL_ENGINE_OBJECT := $(USER_BUILD_DIR)/boring-terminal/terminal.o
TERMINAL_RENDER_OBJECT := $(USER_BUILD_DIR)/boring-terminal/render.o
TERMINAL_INPUT_OBJECT := $(USER_BUILD_DIR)/boring-terminal/input.o
TERMINAL_ELF := $(USER_BUILD_DIR)/boring-terminal.elf
TERMINAL_DEATH_ELF := $(USER_BUILD_DIR)/boring-terminal-death.elf
TERMINAL_HOST_TEST := $(BUILD_DIR)/terminal-host-test
TERMINAL_RENDER_HOST_TEST := $(BUILD_DIR)/terminal-render-host-test

.PHONY: user-boring-terminal user-boring-terminal-death terminal-host-test terminal-render-host-test terminal-audit
user-boring-terminal: $(TERMINAL_ELF)
user-boring-terminal-death: $(TERMINAL_DEATH_ELF)

$(USER_BUILD_DIR)/boring-terminal/%.o: user/boring-terminal/%.c user/boring-terminal/terminal.h user/boring-terminal/render.h user/boring-terminal/input.h user/runtime/include/boring/client.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@

$(TERMINAL_ELF): $(CLIENT_RUNTIME_OBJECT) $(DESKTOP_COMMON) $(DISPLAY_RUNTIME_OBJECT) $(TERMINAL_ENGINE_OBJECT) $(TERMINAL_RENDER_OBJECT) $(TERMINAL_INPUT_OBJECT) $(TERMINAL_MAIN_OBJECT)
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@

$(USER_BUILD_DIR)/boring-terminal-death/main.o: user/boring-terminal/main.c user/boring-terminal/input.h user/boring-terminal/terminal.h user/boring-terminal/render.h user/runtime/include/boring/client.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -DBORING_TERMINAL_DEATH_ACCEPTANCE -c $< -o $@

$(TERMINAL_DEATH_ELF): $(CLIENT_RUNTIME_OBJECT) $(DESKTOP_COMMON) $(DISPLAY_RUNTIME_OBJECT) $(TERMINAL_ENGINE_OBJECT) $(TERMINAL_RENDER_OBJECT) $(TERMINAL_INPUT_OBJECT) $(USER_BUILD_DIR)/boring-terminal-death/main.o
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@

$(TERMINAL_HOST_TEST): tests/terminal-host-test.c user/boring-terminal/terminal.c user/boring-terminal/input.c user/boring-terminal/terminal.h user/boring-terminal/input.h kernel/include/boring/input_abi.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Iuser/boring-terminal -Ikernel/include $(HOST_CFLAGS) tests/terminal-host-test.c user/boring-terminal/terminal.c user/boring-terminal/input.c -o $@

$(TERMINAL_RENDER_HOST_TEST): tests/terminal-render-host-test.c user/boring-terminal/terminal.c user/boring-terminal/render.c user/boring-terminal/terminal.h user/boring-terminal/render.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Iuser/boring-terminal $(HOST_CFLAGS) tests/terminal-render-host-test.c user/boring-terminal/terminal.c user/boring-terminal/render.c -o $@

terminal-host-test: $(TERMINAL_HOST_TEST)
	$(TERMINAL_HOST_TEST)

terminal-render-host-test: $(TERMINAL_RENDER_HOST_TEST)
	$(TERMINAL_RENDER_HOST_TEST)

terminal-audit: $(TERMINAL_ELF)
	sh ./tests/terminal-build-audit.sh

.PHONY: spawn-stack-host-test
spawn-stack-host-test: $(BUILD_DIR)/spawn-stack-host-test
	$<
$(BUILD_DIR)/spawn-stack-host-test: tests/spawn-stack-host-test.c kernel/core/spawn_stack.c kernel/include/boring/spawn_stack.h kernel/include/boring/elf_loader.h kernel/include/boring/syscall_abi.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) tests/spawn-stack-host-test.c kernel/core/spawn_stack.c -o $@

# M39 native editor; reuse the existing bounded glyph renderer and key mapping.
EDIT_MAIN_OBJECT := $(USER_BUILD_DIR)/boring-edit/main.o
EDIT_ENGINE_OBJECT := $(USER_BUILD_DIR)/boring-edit/editor.o
EDIT_ELF := $(USER_BUILD_DIR)/boring-edit.elf
.PHONY: user-boring-edit edit-host-test
user-boring-edit: $(EDIT_ELF)
$(USER_BUILD_DIR)/boring-edit/%.o: user/boring-edit/%.c user/boring-edit/editor.h user/boring-terminal/terminal.h user/boring-terminal/render.h user/boring-terminal/input.h user/runtime/include/boring/client.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@
$(EDIT_ELF): $(CLIENT_RUNTIME_OBJECT) $(DESKTOP_COMMON) $(DISPLAY_RUNTIME_OBJECT) $(TERMINAL_ENGINE_OBJECT) $(TERMINAL_RENDER_OBJECT) $(TERMINAL_INPUT_OBJECT) $(EDIT_ENGINE_OBJECT) $(EDIT_MAIN_OBJECT)
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@
$(BUILD_DIR)/edit-host-test: tests/edit-host-test.c user/boring-edit/editor.c user/boring-edit/editor.h user/boring-terminal/terminal.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -Iuser/boring-edit $(HOST_CFLAGS) tests/edit-host-test.c user/boring-edit/editor.c user/boring-terminal/terminal.c -o $@
edit-host-test: $(BUILD_DIR)/edit-host-test
	./$(BUILD_DIR)/edit-host-test

# M40 native directory browser using existing VFS/display/syscall contracts.
FILES_MAIN_OBJECT := $(USER_BUILD_DIR)/boring-files/main.o
FILES_ENGINE_OBJECT := $(USER_BUILD_DIR)/boring-files/files.o
FILES_ELF := $(USER_BUILD_DIR)/boring-files.elf
.PHONY: user-boring-files files-host-test
user-boring-files: $(FILES_ELF)
$(USER_BUILD_DIR)/boring-files/%.o: user/boring-files/%.c user/boring-files/files.h user/boring-terminal/terminal.h user/boring-terminal/render.h user/runtime/include/boring/client.h
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_USER_CPPFLAGS) $(RUNTIME_USER_CFLAGS) -c $< -o $@
$(FILES_ELF): $(CLIENT_RUNTIME_OBJECT) $(DESKTOP_COMMON) $(DISPLAY_RUNTIME_OBJECT) $(TERMINAL_ENGINE_OBJECT) $(TERMINAL_RENDER_OBJECT) $(FILES_ENGINE_OBJECT) $(FILES_MAIN_OBJECT)
	$(LD) $(DISPLAY_LDFLAGS) $^ -o $@
$(BUILD_DIR)/files-host-test: tests/files-host-test.c user/boring-files/files.c user/boring-files/files.h user/boring-terminal/terminal.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include -Iuser/boring-files $(HOST_CFLAGS) tests/files-host-test.c user/boring-files/files.c user/boring-terminal/terminal.c -o $@
files-host-test: $(BUILD_DIR)/files-host-test
	./$(BUILD_DIR)/files-host-test

.PHONY: client-host-test
client-host-test: $(BUILD_DIR)/client-host-test
	$<
$(BUILD_DIR)/client-host-test: tests/client-host-test.c user/runtime/client.c user/runtime/include/boring/client.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(RUNTIME_USER_CPPFLAGS) $(HOST_CFLAGS) tests/client-host-test.c user/runtime/client.c -o $@

.PHONY: cpu-inventory-host-test
cpu-inventory-host-test: $(BUILD_DIR)/cpu-inventory-host-test
	$<
$(BUILD_DIR)/cpu-inventory-host-test: tests/cpu-inventory-host-test.c kernel/core/cpu_inventory.c kernel/include/boring/cpu_inventory.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) tests/cpu-inventory-host-test.c kernel/core/cpu_inventory.c -o $@

.PHONY: pci-inventory-host-test
pci-inventory-host-test: $(BUILD_DIR)/pci-inventory-host-test
	$<
$(BUILD_DIR)/pci-inventory-host-test: tests/pci-inventory-host-test.c kernel/core/pci_inventory.c kernel/include/boring/pci_inventory.h kernel/include/boring/pci.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) tests/pci-inventory-host-test.c kernel/core/pci_inventory.c -o $@

$(XHCI_HOST_TEST): tests/xhci-host-test.c kernel/core/xhci.c \
		kernel/core/usb_hid.c kernel/include/boring/xhci.h \
		kernel/include/boring/usb_hid.h kernel/include/boring/pci.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) tests/xhci-host-test.c \
		kernel/core/xhci.c kernel/core/usb_hid.c -o $@

.PHONY: smbios-host-test
smbios-host-test: $(BUILD_DIR)/smbios-host-test
	$<
$(BUILD_DIR)/smbios-host-test: tests/smbios-host-test.c kernel/core/smbios.c kernel/include/boring/smbios.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Ikernel/include $(HOST_CFLAGS) tests/smbios-host-test.c kernel/core/smbios.c -o $@
