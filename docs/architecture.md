# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional.

- **Initial architecture:** x86_64.
- **Reference machine:** QEMU `q35`.
- **Bootloader:** Limine 12.5.2, used to load BoringKernel and provide boot-time information that is still needed by the current kernel, including the memory map, HHDM offset and paging mode.
- **Kernel format:** freestanding, statically linked ELF64 x86_64 (`kernel.elf`).
- **Kernel entry path:** firmware → Limine → ELF entry symbol `boring_kernel_entry` → BoringKernel C code.
- **C/assembly boundary:** kernel logic is C. No assembly source file exists. Tiny x86_64 inline assembly is isolated to I/O-port access (`inb`/`outb`), reading `CR3`, invalidating one TLB entry with `invlpg`, and the controlled `cli`/`hlt` stop loop.
- **Serial console:** legacy COM1 at I/O base `0x3f8`, configured for 115200 baud, 8 data bits, no parity, one stop bit; transmit is polled.
- **Privilege level:** the kernel currently executes at x86_64 CPL 0 (ring 0), as handed off by Limine. There is no ring-3/userspace execution yet.

## Physical memory manager

BoringKernel has a minimal physical page-frame allocator.

- **Page size:** 4096 bytes.
- **Source of ownership information:** the Limine memory-map response requested by BoringKernel at boot.
- **Accepted allocatable memory type:** only `LIMINE_MEMMAP_USABLE` / type `0`.
- **Reserved treatment:** all other Limine memory-map types remain non-allocatable, including reserved, ACPI, bad-memory, bootloader-reclaimable, executable/module, framebuffer and reserved-mapped regions.
- **Kernel protection:** Limine reports the loaded kernel/executable area separately from usable RAM; because the PMM accepts only type `USABLE`, BoringKernel image pages are never added to the free-frame set.
- **Boot-structure protection:** bootloader-reclaimable memory is deliberately not reclaimed yet, so Limine-owned boot structures cannot be handed out accidentally.
- **PMM metadata:** a fixed bitmap and region table live in BoringKernel static storage (`.bss`), inside the loaded kernel image rather than inside allocatable usable memory.
- **Allocator strategy:** deterministic first-fit over validated page-aligned usable regions.
- **Tracking capacity:** 1,048,576 frames, corresponding to at most 4 GiB of managed usable RAM in this implementation. Exceeding this limit makes PMM initialization fail explicitly.
- **Free semantics:** only page-aligned addresses belonging to a managed usable region and currently marked allocated may be freed. Invalid and double frees fail.

Before accepting the map, the PMM rejects null/empty/oversized maps, null entries, zero-length entries, `base + length` overflow and overlapping memory-map entries. Usable ranges are rounded inward to 4-KiB boundaries. No physical address is returned unless it belongs to one of those normalized usable ranges.

## Virtual memory manager

BoringKernel controls selected x86_64 virtual-to-physical mappings while deliberately retaining the bootloader-created execution environment that is still required.

### Paging mode and active root

- **Paging mode:** x86_64 four-level paging. BoringKernel explicitly requests Limine's four-level mode and rejects any response that reports a different mode.
- **Page size:** 4096-byte pages only.
- **Active root:** BoringKernel reads `CR3`, masks out non-address bits and adopts the physical address of the currently active PML4. It does not create, load or switch to a replacement root yet.
- **Inherited mappings:** the running kernel image, current kernel stack, HHDM, Limine responses/boot structures and all other mappings needed to keep the current execution environment alive remain inherited from Limine.
- **Owned mappings:** BoringKernel may add and remove selected four-level 4-KiB mappings inside the active address space. This is partial page-table ownership, not complete address-space ownership.

### Physical access to page tables

The VMM never assumes that a physical address can be dereferenced directly.

BoringKernel requests Limine's Higher Half Direct Map (HHDM) offset and centralizes physical-to-HHDM conversion in the x86_64 VMM. Conversions are rejected on overflow, non-canonical resulting addresses or when the physical page is not covered by a memory-map type that the current Limine HHDM is expected to map for this kernel configuration.

New page-table frames are obtained exclusively through the existing PMM. Because those frames come from `USABLE` memory, they can be accessed and zeroed through the HHDM before being installed as page-table pages. The inherited active root and inherited child tables are not claimed as PMM-owned memory and are never freed by the VMM.

### PMM/VMM boundary

```text
PMM
→ owns physical 4-KiB frames

VMM
→ owns selected virtual-to-physical mappings
→ requests page-table frames from PMM
```

The VMM keeps a small static list of page-table frames that it allocated itself. On unmap, an empty PT/PD/PDPT is returned to the PMM only when that table is in this owned list. Inherited Limine tables are never reclaimed merely because they happen to become empty.

### Early VMM test region

The deliberately reserved early test window is:

```text
[0xffffff0000000000, 0xffffff0000200000)
```

This is a 2-MiB window in PML4 slot 510. The linked BoringKernel image begins at `0xffffffff80000000`, in PML4 slot 511. VMM initialization verifies that the chosen test window does not overlap the currently described HHDM span, and the self-test proves that the specific test page is initially unmapped before installing anything there.

The test address is defined once in the x86_64 VMM rather than scattered through kernel code.

### Current mapping semantics

The current VMM supports only:

- present 4-KiB mappings;
- writable or read-only kernel mappings;
- walking PML4 → PDPT → PD → PT;
- allocating missing PDPT/PD/PT frames through PMM;
- translation of ordinary 4-KiB leaf mappings;
- duplicate-map rejection;
- absent-unmap rejection;
- `invlpg` after mapping changes;
- release of empty VMM-owned page-table frames.

It rejects unaligned addresses, unsupported mapping flags, malformed/non-present-but-nonzero entries and huge-page leaves on paths that this implementation cannot safely interpret.

The QEMU acceptance self-test allocates one PMM frame, maps it writable into the reserved test window, translates it back to the same physical frame, writes and reads `0x424F52494E474F53` through the virtual address, unmaps it, verifies that translation then fails, frees the data frame, and verifies that VMM-owned page-table frames and PMM free-frame bookkeeping return to their pre-test values.

## Kernel heap

BoringKernel has a small bounded dynamic allocator for byte-sized kernel allocations. The layers remain separate:

```text
kmalloc / kfree
    ↓
kernel heap
    ↓
VMM mappings
    ↓
PMM physical frames
```

The heap never bypasses PMM or VMM and introduces no libc dependency.

### Reserved virtual range and backing

The heap owns the half-open virtual range:

```text
[0xffffff0000200000, 0xffffff0001200000)
```

This is a finite 16-MiB range immediately after the separate 2-MiB VMM test window. It remains below the linked kernel range in PML4 slot 511. Before mapping every heap page, the heap asks the VMM to confirm that the target virtual page is currently unmapped.

Heap initialization maps exactly **two 4096-byte pages**. Each heap page is backed by a physical frame allocated through PMM and installed as a writable page through `vmm_map_page`; the mapped page is then explicitly zeroed before allocator metadata or payload uses it.

When no free block can satisfy an allocation, the heap grows by exactly one 4096-byte page. Growth stops at the 16-MiB virtual limit and fails cleanly instead of wrapping or mapping outside the reserved range. No page faults or demand paging are involved.

The verified QEMU run used five PMM frames for heap initialization: two heap data frames plus three VMM-owned page-table frames required to establish the new heap mapping hierarchy. The self-test then forced exactly one additional heap page mapping and one additional PMM data frame. No additional page-table frame was needed for that adjacent page.

### Allocation algorithm and metadata

The allocator is deliberately simple and deterministic:

- first-fit block selection;
- 16-byte payload alignment;
- block metadata stored inside the heap itself;
- splitting only when the remainder can contain another 48-byte block header plus at least 16 bytes of payload;
- immediate coalescing of adjacent free blocks;
- `kmalloc(0)` returns `NULL`;
- allocation-size rounding, region growth and pointer arithmetic are bounds/overflow checked.

Each block has a 48-byte header:

```text
magic
payload size
previous block pointer
next block pointer
allocation/free state
guard value
```

The block chain is kept in address order and must describe the entire mapped heap without gaps or overlapping blocks. Validation checks header magic, guard value, size and alignment, mapped-range containment, previous/next consistency, exact block adjacency and the invariant that adjacent free blocks have already been coalesced. A detected structural inconsistency marks the heap corrupted and allocator operations stop rather than knowingly continuing with damaged metadata.

This is basic corruption detection for development, not a hardened production allocator.

### Free and reuse semantics

`kfree` accepts only an exact payload pointer belonging to a currently allocated block. It scans the validated block chain rather than deriving and trusting a header from an arbitrary caller-provided address.

It rejects:

- `NULL`;
- pointers outside the currently mapped heap;
- unaligned pointers;
- interior pointers;
- pointers that are not exact allocation starts;
- a second free of an already freed allocation.

Freed blocks are immediately coalesced with adjacent free blocks where possible. Deterministic first-fit behavior allowed the QEMU self-test to free a 64-byte allocation and then verify that a new 64-byte allocation reused the exact same payload address.

Backing pages are intentionally **retained once mapped**. `kfree` returns payload capacity to the block allocator but does not unmap completely free heap pages or return their data frames to PMM. Heap shrinking is a separate possible future optimization, not part of this bootstrap allocator.

### Verified heap self-test

The QEMU acceptance path allocates real blocks of 1, 16, 64, 200, 6000 and 4096 bytes, verifies 16-byte alignment and pairwise non-overlap, then writes and reads every requested byte using distinct patterns (`0xAA`, `0x55`, an incrementing byte pattern, `0xA5`, an offset pattern and `0x3C`).

The allocations force growth beyond the initial two pages. The test verifies the new page has a VMM translation to a PMM-usable physical frame. It then checks valid free, deterministic reuse, double-free rejection, an outside-heap free, an aligned interior-pointer free, and final allocator bookkeeping.

After all test allocations are released and coalesced, three pages remain mapped by policy: 12,288 mapped bytes, zero used payload bytes and 12,240 free payload bytes. The 48-byte difference is the single remaining block header.

## Still not implemented

BoringKernel does **not** yet own the complete address space. It still depends on Limine-created mappings and the Limine HHDM for the current execution environment and page-table access.

The kernel heap is a small single-core bootstrap allocator, not a finished production allocator. It has no locking/SMP support, slab/object caches, per-CPU allocation, guard pages, allocation tagging, page-level shrinking, demand paging or sophisticated hardening.

There is still no virtual-address allocator, page-fault handler/IDT, huge-page support, userspace mapping, per-process address space, NX policy, copy-on-write, demand paging, swap, page-table locking, scheduler, threads, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, BoringFS implementation, storage stack, input, graphics, BoringWM, networking, USB, audio or GPU acceleration.

The next separate roadmap milestone is controlled x86_64 exception infrastructure. No IDT or exception-handling code is part of the heap milestone.
