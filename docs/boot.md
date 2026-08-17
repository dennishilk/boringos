# BoringOS boot — Phase 0.1

The first boot path is deliberately small:

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
identity lines
        ↓
controlled cli/hlt loop
```

Limine is an external bootloader, not a kernel component. After it transfers control to the ELF entry point, the required serial output is produced by BoringKernel itself.

The build creates a hybrid Limine ISO with BIOS and x86_64 UEFI boot files. The Phase 0.1 automated acceptance run uses QEMU's normal x86_64 PC firmware path; UEFI is not a separate acceptance claim in this milestone.

The kernel is linked in the x86_64 higher-half region expected by the Limine protocol. No BoringKernel memory manager is introduced merely to satisfy this milestone; the bootloader-established execution environment is accepted only for initial handoff.

## Reproducibility and provenance

- Limine binary release is pinned to **12.5.2** and its downloaded archive is checked against SHA-256 `4c760c09c53560d859b362319a3dc63b79cca3d47f35d69ab0106a13b8057055`.
- The small Limine request-marker/base-revision constants in `kernel/include/boring/boot_protocol.h` come from the Limine boot protocol, reference commit `80ef54bed402b8c0b672a707c1df4c532f3428ad` (0BSD).
- The ELF section arrangement follows the requirements demonstrated by the official Limine x86_64 C template, but the BoringKernel source and entry implementation are BoringOS code.

No Linux, BSD, Redox or other operating-system kernel code is imported.
