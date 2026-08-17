# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional.

- **Initial architecture:** x86_64.
- **Reference machine:** QEMU `q35`.
- **Bootloader:** Limine 12.5.2, used only to load and transfer control to BoringKernel and to provide boot-time information such as the memory map.
- **Kernel format:** freestanding, statically linked ELF64 x86_64 (`kernel.elf`).
- **Kernel entry path:** firmware → Limine → ELF entry symbol `boring_kernel_entry` → BoringKernel C code.
- **C/assembly boundary:** kernel logic is C. No assembly source file exists. Tiny x86_64 inline assembly remains isolated to I/O-port access (`inb`/`outb`) and the controlled `cli`/`hlt` stop loop.
- **Serial console:** legacy COM1 at I/O base `0x3f8`, configured for 115200 baud, 8 data bits, no parity, one stop bit; transmit is polled.
- **Privilege level:** the kernel currently executes at x86_64 CPL 0 (ring 0), as handed off by Limine. There is no ring-3/userspace execution yet.

## Physical memory manager

BoringKernel now has a minimal physical page-frame allocator.

- **Page size:** 4096 bytes.
- **Source of ownership information:** the Limine memory-map response requested by BoringKernel at boot.
- **Accepted allocatable memory type:** only `LIMINE_MEMMAP_USABLE` / type `0`.
- **Reserved treatment:** all other Limine memory-map types remain non-allocatable, including reserved, ACPI, bad-memory, bootloader-reclaimable, executable/module, framebuffer and reserved-mapped regions.
- **Kernel protection:** Limine reports the loaded kernel/executable area separately from usable RAM; because the PMM accepts only type `USABLE`, BoringKernel image pages are never added to the free-frame set.
- **Boot-structure protection:** bootloader-reclaimable memory is deliberately not reclaimed in this milestone, so Limine-owned boot structures cannot be handed out accidentally.
- **PMM metadata:** a fixed bitmap and region table live in BoringKernel static storage (`.bss`), inside the loaded kernel image rather than inside allocatable usable memory.
- **Allocator strategy:** deterministic first-fit over validated page-aligned usable regions.
- **Tracking capacity:** 1,048,576 frames, corresponding to at most 4 GiB of managed usable RAM in this first implementation. Exceeding this limit makes PMM initialization fail explicitly.
- **Free semantics:** only page-aligned addresses belonging to a managed usable region and currently marked allocated may be freed. Invalid and double frees fail.

Before accepting the map, the PMM rejects null/empty/oversized maps, null entries, zero-length entries, `base + length` overflow and overlapping memory-map entries. Usable ranges are rounded inward to 4-KiB boundaries. No physical address is returned unless it belongs to one of those normalized usable ranges.

The PMM answers only which physical page frames are available. It does **not** create virtual mappings or take ownership of page tables.

## Still not implemented

BoringKernel still has no general-purpose kernel heap, BoringKernel-owned virtual-memory manager, IDT, interrupts, timer, scheduler, threads, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, BoringFS implementation, storage stack, input, graphics, BoringWM, networking, USB, audio or GPU acceleration.

The next memory-management phase is BoringKernel-owned virtual memory/page-table management. That work is intentionally not part of the PMM milestone.
