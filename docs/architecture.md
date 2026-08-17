# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional.

- **Initial architecture:** x86_64.
- **Reference machine:** QEMU `q35`.
- **Bootloader:** Limine 12.5.2, used to load BoringKernel and provide boot-time information that is still needed by the current kernel, including the memory map, HHDM offset and paging mode.
- **Kernel format:** freestanding, statically linked ELF64 x86_64 (`kernel.elf`).
- **Kernel entry path:** firmware → Limine → ELF entry symbol `boring_kernel_entry` → BoringKernel C code.
- **C/assembly boundary:** kernel policy and exception diagnostics remain in C. Tiny x86_64 inline assembly remains isolated to I/O-port access (`inb`/`outb`), reading `CR3`, invalidating one TLB entry with `invlpg`, and the controlled `cli`/`hlt` stop loop. The exception milestone adds one architecture-specific source file, `kernel/arch/x86_64/exception_stubs.S`, containing only exception entry normalization, required descriptor-register instructions and deliberate hardware-fault triggers used by QEMU acceptance tests.
- **Serial console:** legacy COM1 at I/O base `0x3f8`, configured for 115200 baud, 8 data bits, no parity, one stop bit; transmit is polled.
- **Privilege level:** the kernel currently executes at x86_64 CPL 0 (ring 0), as handed off by Limine. There is no ring-3/userspace execution yet.

## Physical memory manager

BoringKernel has a minimal physical page-frame allocator.

- **Page size:** 4096 bytes.
- **Source of ownership information:** the Limine memory-map response requested by BoringKernel at boot.
- **Accepted allocatable memory type:** only `LIMINE_MEMMAP_USABLE` / type `0`.
- **Reserved treatment:** all other Limine memory-map types remain non-allocatable, including reserved, ACPI, bad-memory, bootloader-reclaimable, executable/module, framebuffer and reserved-mapped regions.
- **Kernel protection:** because the PMM accepts only type `USABLE`, BoringKernel image pages are never added to the free-frame set.
- **Boot-structure protection:** bootloader-reclaimable memory is deliberately not reclaimed yet.
- **PMM metadata:** a fixed bitmap and region table live in BoringKernel static storage (`.bss`).
- **Allocator strategy:** deterministic first-fit over validated page-aligned usable regions.
- **Tracking capacity:** 1,048,576 frames, corresponding to at most 4 GiB of managed usable RAM in this implementation.
- **Free semantics:** only page-aligned addresses belonging to a managed usable region and currently marked allocated may be freed. Invalid and double frees fail.

Before accepting the map, the PMM rejects null/empty/oversized maps, null entries, zero-length entries, `base + length` overflow and overlapping memory-map entries. Usable ranges are rounded inward to 4-KiB boundaries.

## Virtual memory manager

BoringKernel controls selected x86_64 virtual-to-physical mappings while deliberately retaining the bootloader-created execution environment that is still required.

### Paging mode and active root

- **Paging mode:** x86_64 four-level paging.
- **Page size:** 4096-byte pages only.
- **Active root:** BoringKernel reads `CR3`, masks out non-address bits and adopts the physical address of the currently active PML4. It does not create or load a replacement root yet.
- **Inherited mappings:** kernel image, current kernel stack, HHDM, Limine responses/boot structures and other boot-critical mappings remain inherited from Limine.
- **Owned mappings:** BoringKernel may add and remove selected four-level 4-KiB mappings. This is partial page-table ownership, not complete address-space ownership.

### Physical access to page tables

BoringKernel requests Limine's Higher Half Direct Map (HHDM) offset and centralizes physical-to-HHDM conversion in the x86_64 VMM. New page-table frames are obtained exclusively through PMM and zeroed through the HHDM before use. Inherited Limine tables are not claimed as PMM-owned memory and are never reclaimed merely because they become empty.

### PMM/VMM boundary

```text
PMM
→ owns physical 4-KiB frames

VMM
→ owns selected virtual-to-physical mappings
→ requests page-table frames from PMM
```

The VMM tracks page-table frames it allocated itself and returns an empty PT/PD/PDPT to PMM only when that table is VMM-owned.

### Early VMM test region

The deliberately reserved early test window is:

```text
[0xffffff0000000000, 0xffffff0000200000)
```

This is a 2-MiB window in PML4 slot 510. The linked BoringKernel image begins at `0xffffffff80000000`, in PML4 slot 511. The VMM self-test uses the first page at `0xffffff0000000000`, maps it, performs a real write/read, unmaps it and verifies that it is absent again.

### Current mapping semantics

The current VMM supports present 4-KiB kernel mappings, writable/read-only flags, ordinary PML4 → PDPT → PD → PT walks, allocation of missing tables through PMM, translation, duplicate-map rejection, absent-unmap rejection, `invlpg`, and reclamation of empty VMM-owned page tables. It does not provide demand paging, huge-page management, per-process address spaces or complete address-space ownership.

## Kernel heap

BoringKernel has a small bounded dynamic allocator for byte-sized kernel allocations:

```text
kmalloc / kfree
    ↓
kernel heap
    ↓
VMM mappings
    ↓
PMM physical frames
```

### Reserved virtual range and backing

The heap owns:

```text
[0xffffff0000200000, 0xffffff0001200000)
```

This is a finite 16-MiB range immediately after the separate VMM test window. Initialization maps exactly two 4096-byte pages. Each backing frame comes from PMM and every mapping is installed through VMM. When no free block can satisfy an allocation, the heap grows by exactly one 4096-byte page until the finite limit is reached.

### Allocation algorithm and metadata

The allocator uses deterministic first-fit selection, 16-byte payload alignment, 48-byte in-heap block headers, splitting when a useful remainder exists and immediate coalescing of adjacent free blocks. `kmalloc(0)` returns `NULL`. Block topology, magic, guards, sizes, links, bounds and alignment are validated before allocator operations continue.

`kfree` accepts only the exact payload start of a currently allocated block. Outside-heap, unaligned/interior and double frees are rejected. Mapped heap pages are retained after `kfree`; page-level heap shrinking is not implemented.

The QEMU heap self-test performs real allocations of 1, 16, 64, 200, 6000 and 4096 bytes, verifies write/read behavior, forces PMM/VMM-backed growth, proves deterministic reuse and verifies final bookkeeping. After all test allocations are released, three pages remain mapped by policy with zero used payload bytes and 12,240 free payload bytes.

## x86_64 IDT and CPU exception handling

BoringKernel now installs its own x86_64 Interrupt Descriptor Table and can diagnose fatal CPU exceptions without relying on a silent reset or missing serial output.

The verified path is:

```text
CPU exception
    ↓
BoringKernel IDT
    ↓
x86_64 exception stub
    ↓
normalized trap frame
    ↓
BoringKernel C exception handler
    ↓
COM1 diagnostic
    ↓
controlled cli/hlt halt
```

### IDT layout

The kernel allocates a statically aligned **256-entry IDT**. Each x86_64 descriptor is 16 bytes, so the loaded table is 4096 bytes and the IDTR limit is **4095**.

Only vectors **0–31** receive active exception handlers in this milestone. Entries 32–255 remain zero/unconfigured. No hardware IRQ vector is claimed to work.

Each configured exception descriptor uses:

- present, DPL0 **64-bit interrupt gate** attributes `0x8e`;
- IST index 0;
- the kernel's **currently executing CS selector**, read at runtime rather than hard-coded;
- the corresponding BoringKernel exception stub address.

Before `lidt`, BoringKernel validates all 32 configured descriptors. The load helper executes `cli` before `lidt`, because IRQ vectors are deliberately not configured yet. The kernel then executes `sidt` and rejects initialization unless the CPU-reported base and limit exactly match the requested IDTR.

A verified QEMU run reported:

```text
IDT entries: 32
IDTR base: 0xFFFFFFFF800268A0
IDTR limit: 4095
Code selector: 0x0000000000000028
IDT: loaded
Exceptions: online
```

The IDTR base is a linked runtime address and is useful as acceptance evidence, not a stable ABI value.

### Exception names and vectors

Vectors 0–31 all have stubs. Architecturally defined exceptions receive descriptive names, including Divide Error, Debug, NMI, Breakpoint, Overflow, Bound Range Exceeded, Invalid Opcode, Device Not Available, Double Fault, Invalid TSS, Segment Not Present, Stack-Segment Fault, General Protection Fault, Page Fault, x87 Floating-Point Exception, Alignment Check, Machine Check, SIMD Floating-Point Exception, Virtualization Exception and Control Protection Exception.

Reserved slots are explicitly labeled reserved rather than assigned invented semantics. The implementation also names architecture/vendor-defined vectors such as Hypervisor Injection Exception and VMM Communication Exception where applicable; receiving any vector still enters the same bounded fatal diagnostic policy unless the dedicated Double Fault path applies.

### C / assembly boundary

High-level exception policy remains in `kernel/arch/x86_64/exception.c`: IDT construction/validation, exception naming, diagnostic formatting, Page Fault decoding, Double Fault reporting and fatal-stop policy are C.

`kernel/arch/x86_64/exception_stubs.S` contains only work that is tied directly to the x86_64 exception ABI:

- one tiny entry stub per vector 0–31;
- hardware-error-code normalization;
- GPR preservation;
- construction of the current CPL0 trap-frame contract;
- call into the shared C dispatcher;
- `lidt` / `sidt`;
- reads of `CS` and `CR2`;
- deliberate `divq` and unmapped-memory-load instructions used by the two fatal QEMU acceptance modes.

There is no general assembly runtime or interrupt framework.

### Normalized trap frame

Some x86_64 exceptions push a hardware error code and some do not. For vectors without one, the entry stub pushes a synthetic zero. For vectors with one, the stub preserves the CPU-pushed error code. The stub then pushes the vector number, saves GPRs and presents C with one predictable 176-byte structure:

```text
offset   field
0x00     RSP
0x08     SS
0x10     R15
0x18     R14
0x20     R13
0x28     R12
0x30     R11
0x38     R10
0x40     R9
0x48     R8
0x50     RSI
0x58     RDI
0x60     RBP
0x68     RDX
0x70     RCX
0x78     RBX
0x80     RAX
0x88     vector
0x90     error code
0x98     RIP
0xA0     CS
0xA8     RFLAGS
```

The current hardware-error-code stub set is vectors **8, 10, 11, 12, 13, 14, 17, 21, 29 and 30**. Other vectors receive the synthetic zero.

BoringKernel currently handles only CPL0 → CPL0 exceptions. In that case x86_64 does not push privilege-transition `RSP`/`SS` fields. The assembly therefore computes the interrupted ring-0 RSP from the known normalized frame depth and reads the current `SS` selector explicitly. It does **not** read nonexistent stack slots. When real ring-3 transitions are introduced later, this trap-frame contract must be extended to distinguish CPU-pushed privilege-transition state.

### Fatal exception policy

All exceptions handled by this milestone are fatal. The shared C handler prints vector, name, normalized error code, RIP, CS, RFLAGS, RSP, SS and saved GPR state. It does not return to the interrupted instruction. It ends with:

```text
Fatal exception: controlled halt.
```

followed by the existing `x86_64_halt_forever()` path (`cli` plus an infinite `hlt` loop).

No exception is used to implement demand paging, retry execution or silently repair kernel state.

### Page Fault diagnostics

Vector 14 additionally reads `CR2` and reports the faulting virtual address. The current Page Fault decoder reports the architecturally relevant low error-code fields used by this kernel mode:

- present/protection violation versus non-present;
- read versus write;
- supervisor versus user;
- reserved-bit violation;
- instruction fetch.

The dedicated QEMU Page Fault mode first verifies that `0xffffff0000000000` is unmapped through VMM and then executes a real load from that address. The CPU/MMU produced:

```text
Vector: 14
Name: Page Fault
Error code: 0x0000000000000000
RIP: 0xFFFFFFFF80004544
CR2: 0xFFFFFF0000000000
Page fault mapping: non-present
Page fault access: read
Page fault privilege: supervisor
Page fault reserved-bit violation: no
Page fault instruction fetch: no
Fatal exception: controlled halt.
```

Error code zero is therefore consistent with the deliberately unmapped supervisor read used by this acceptance test. No mapping is created in response to the fault.

### Divide Error acceptance

The dedicated divide mode executes a real `divq` with a zero divisor in architecture-specific test code. It does not call the C handler directly. QEMU reported:

```text
Vector: 0
Name: Divide Error
Error code: 0x0000000000000000
RIP: 0xFFFFFFFF8000450F
CS: 0x0000000000000028
RSP: 0xFFFF800007F8FD60
SS: 0x0000000000000030
Fatal exception: controlled halt.
```

This proves CPU #DE → IDT vector 0 → BoringKernel stub → C handler → serial diagnostic → controlled halt.

### Double Fault limitation

Vector 8 has a dedicated minimal C diagnostic that avoids routing through additional recovery logic. It reports Double Fault, the hardware error code, RIP/RSP/RFLAGS, states that the normal kernel stack is in use, and halts.

This is **not a hardened Double Fault design**. There is no TSS/IST configuration and no dedicated emergency stack. If the current stack itself is unusable, the vector-8 path may be unable to execute. A robust future Double Fault path is expected to require a separately provisioned IST stack once the required TSS/privilege infrastructure is introduced.

### Acceptance build modes

The Makefile has three compile-time development modes:

```text
TEST_MODE=normal
TEST_MODE=divide
TEST_MODE=pagefault
```

`normal` runs the complete PMM, VMM and heap acceptance sequence, installs/verifies the IDT and halts normally without creating a fatal fault. `divide` and `pagefault` first pass the same earlier subsystem checks and IDT initialization, then intentionally trigger their respective real CPU faults. A mode-stamp dependency forces kernel objects to rebuild when the mode changes, preventing stale test-mode objects.

CI runs all three QEMU modes with timeouts and treats the deliberate controlled fatal halt as success only when the expected vector-specific diagnostics were observed. Missing diagnostics, wrong vectors, early QEMU termination or failure markers are not accepted as success.

## Still not implemented

BoringKernel does **not** yet own the complete address space. It still depends on Limine-created mappings and the Limine HHDM for the current execution environment and page-table access.

The kernel heap remains a small single-core bootstrap allocator, not a production allocator.

The exception subsystem is fatal-diagnostic infrastructure only. There is still **no hardware IRQ handling**, PIC remapping, APIC/IOAPIC setup, IRQ routing, PIT/HPET/timer interrupt, TSS/IST exception-stack hardening, scheduler, preemption, threads, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, BoringFS implementation, storage stack, input, graphics, BoringWM, networking, USB, audio or GPU acceleration.

The next separate roadmap milestone is hardware interrupt-controller and timer work. It has not been started and requires a separate implementation instruction.
