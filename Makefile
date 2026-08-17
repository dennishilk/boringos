SHELL := /bin/sh

BUILD_DIR := build
KERNEL_BUILD_DIR := $(BUILD_DIR)/kernel
ISO_ROOT := $(BUILD_DIR)/iso_root
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/boringos.iso

CC = gcc
LD = ld
HOST_CC = cc
QEMU = qemu-system-x86_64
CURL = curl
XORRISO = xorriso

TEST_MODE ?= normal
ifeq ($(TEST_MODE),normal)
TEST_MODE_VALUE := 0
else ifeq ($(TEST_MODE),divide)
TEST_MODE_VALUE := 1
else ifeq ($(TEST_MODE),pagefault)
TEST_MODE_VALUE := 2
else
$(error unsupported TEST_MODE '$(TEST_MODE)'; use normal, divide, or pagefault)
endif

LIMINE_VERSION := 12.5.2
LIMINE_ARCHIVE := $(BUILD_DIR)/deps/limine-binary.tar.gz
LIMINE_DIR := $(BUILD_DIR)/deps/limine-binary
LIMINE_SHA256 := 4c760c09c53560d859b362319a3dc63b79cca3d47f35d69ab0106a13b8057055
LIMINE_URL := https://github.com/Limine-Bootloader/Limine/releases/download/v$(LIMINE_VERSION)/limine-binary.tar.gz

CPPFLAGS := -Ikernel/include -DBORING_TEST_MODE=$(TEST_MODE_VALUE)
CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-m64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
	-mno-red-zone -mcmodel=kernel -O2 -g \
	-Wall -Wextra -Wpedantic -Werror \
	-Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
ASFLAGS := -ffreestanding -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker/x86_64.ld

KERNEL_C_SOURCES := \
	kernel/core/entry.c \
	kernel/core/pmm.c \
	kernel/core/heap.c \
	kernel/arch/x86_64/vmm.c \
	kernel/arch/x86_64/exception.c \
	kernel/arch/x86_64/serial.c \
	kernel/arch/x86_64/cpu.c
KERNEL_ASM_SOURCES := \
	kernel/arch/x86_64/exception_stubs.S
KERNEL_C_OBJECTS := $(patsubst %.c,$(KERNEL_BUILD_DIR)/%.o,$(KERNEL_C_SOURCES))
KERNEL_ASM_OBJECTS := $(patsubst %.S,$(KERNEL_BUILD_DIR)/%.o,$(KERNEL_ASM_SOURCES))
KERNEL_OBJECTS := $(KERNEL_C_OBJECTS) $(KERNEL_ASM_OBJECTS)
MODE_STAMP := $(BUILD_DIR)/.test-mode-$(TEST_MODE)

.PHONY: all kernel run run-headless test clean distclean

all: $(ISO)

kernel: $(KERNEL_ELF)

$(MODE_STAMP):
	@mkdir -p $(BUILD_DIR)
	@rm -f $(BUILD_DIR)/.test-mode-*
	@touch $@

$(KERNEL_BUILD_DIR)/%.o: %.c $(MODE_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(KERNEL_BUILD_DIR)/%.o: %.S $(MODE_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJECTS) kernel/linker/x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $@

$(LIMINE_ARCHIVE):
	@mkdir -p $(dir $@)
	$(CURL) --fail --location --retry 3 --output $@ $(LIMINE_URL)
	printf '%s  %s\n' '$(LIMINE_SHA256)' '$@' | sha256sum -c -

$(LIMINE_DIR)/limine: $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	tar -xzf $(LIMINE_ARCHIVE) -C $(BUILD_DIR)/deps
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)"

$(ISO): $(KERNEL_ELF) $(LIMINE_DIR)/limine limine.conf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp limine.conf $(ISO_ROOT)/boot/limine/limine.conf
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
	$(QEMU) -M q35 -m 128M -cdrom $(ISO) -boot d \
		-display none -serial stdio -monitor none -no-reboot -no-shutdown

run-headless: run

test:
	./tests/boot-qemu.sh
	./tests/exception-divide-qemu.sh
	./tests/exception-pagefault-qemu.sh

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
