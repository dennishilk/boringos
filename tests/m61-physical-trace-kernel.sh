#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

# Re-link only the M61 physical kernel after the established runtime/root build.
# This keeps the diagnostic surface narrow and avoids rebuilding unrelated CI.
rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS='-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M61_PHYSICAL_BREADCRUMBS=1' \
    TEST_HARNESS_C='kernel/core/m61_desktop_test.c kernel/core/m37_desktop_test_adapter.c kernel/core/block_slice.c kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/usb_mass_storage.c kernel/core/m61_physical_breadcrumbs.c' \
    LD='ld --wrap=serial_init --wrap=boring_cpu_inventory_init --wrap=boring_pci_inventory_init --wrap=boring_smbios_boot_init --wrap=pmm_init --wrap=vmm_init --wrap=heap_init --wrap=exception_init --wrap=syscall_test_run --wrap=boring_input_init --wrap=irq_init --wrap=timer_init --wrap=xhci_init --wrap=xhci_address_connected --wrap=xhci_discover_descriptors --wrap=xhci_configure_hid_devices_mixed --wrap=usb_mass_storage_init --wrap=boringfs_vfs_create_writable --wrap=process_set_name --wrap=boring_framebuffer_user_claim --wrap=boring_framebuffer_user_present --wrap=boring_ipc_service_register --wrap=x86_64_exception_dispatch' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m61-usb.conf \
    build/kernel.elf build/deps/limine-binary/limine

nm build/kernel.elf | grep -Fq 'boring_m61_physical_breadcrumbs_enabled'
printf '%s\n' 'M61 direct framebuffer text boot trace: ENABLED'
