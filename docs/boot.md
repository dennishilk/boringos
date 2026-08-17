# BoringOS boot — current bootstrap path

The current boot path remains deliberately small, but memory management now has two verified stages:

```text
QEMU x86_64 firmware
        ↓
Limine 12.5.2
        ↓
build/kernel.elf
        ↓
boring_kernel_entry
        ↓
COM1 serial initialization
        ↓
Limine memory map
        ↓
BoringKernel PMM
        ↓
PMM self-test
        ↓
Limine HHDM + x86_64 paging-mode information
        ↓
BoringKernel selected VMM mappings
        ↓
VMM write/read/map/unmap self-test
        ↓
controlled cli/hlt loop
```

Limine is an external bootloader, not a BoringKernel component. After it transfers control to the ELF entry point, the kernel's serial output, PMM and VMM logic are BoringOS code.

The build creates a hybrid Limine ISO with BIOS and x86_64 UEFI boot files. The automated acceptance run currently uses QEMU's normal x86_64 PC firmware path; UEFI is not a separate acceptance claim at this stage.

The kernel is linked in the x86_64 higher-half region expected by the Limine protocol. BoringKernel now owns allocation of usable physical 4-KiB frames and can create/remove selected 4-KiB virtual mappings. It intentionally does **not** replace the bootloader-created active root page table yet. Kernel execution, the current stack, HHDM and boot structures still rely on mappings inherited from Limine.

## Boot information still consumed from Limine

The current kernel requests and validates:

- the memory map for PMM ownership decisions;
- the HHDM offset so physical page-table frames can be accessed without assuming identity mapping;
- x86_64 four-level paging mode for the current VMM milestone.

The VMM reads the currently active root from `CR3` and modifies only selected mappings. See [`architecture.md`](architecture.md) for the exact ownership boundary and reserved early VMM test range.

## Reproducibility and provenance

- Limine binary release is pinned to **12.5.2** and its downloaded archive is checked against SHA-256 `4c760c09c53560d859b362319a3dc63b79cca3d47f35d69ab0106a13b8057055`.
- The small Limine request/response declarations in `kernel/include/boring/boot_protocol.h` come from the Limine boot protocol (0BSD). No Limine implementation code is part of BoringKernel.
- The ELF section arrangement follows requirements demonstrated by the official Limine x86_64 C template, while BoringKernel entry, PMM and VMM implementations are BoringOS code.

No Linux, BSD, Redox or other operating-system kernel code is imported.
