# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional.

- **Initial architecture:** x86_64.
- **Reference machine:** QEMU `q35`. For the current legacy PIC/PIT bootstrap proof, the single reference CPU is explicitly `qemu64,apic=off`; BoringKernel does not configure a LAPIC in this milestone.
- **Bootloader:** Limine 12.5.2, used to load BoringKernel and provide boot-time information that is still needed by the current kernel, including the memory map, HHDM offset and paging mode.
- **Kernel format:** freestanding, statically linked ELF64 x86_64 (`kernel.elf`).
- **Kernel entry path:** firmware → Limine → ELF entry symbol `boring_kernel_entry` → BoringKernel C code.
- **C/assembly boundary:** kernel policy, allocation, exception diagnostics and IRQ dispatch remain in C. Tiny x86_64 inline assembly is isolated to CPU/I/O primitives such as port I/O, `CR3`, `invlpg`, IF control and the halt loop. `exception_stubs.S` contains exception ABI mechanics and deliberate fault triggers; `irq_stubs.S` contains only hardware-IRQ entry/save/restore/`iretq` mechanics.
- **Serial console:** legacy COM1 at I/O base `0x3f8`, configured for 115200 baud, 8 data bits, no parity, one stop bit; transmit is polled.
- **Privilege level:** the kernel currently executes at x86_64 CPL 0 (ring 0), as handed off by Limine. There is no ring-3/userspace execution yet.
- **Execution model:** one bootstrap CPU only. There is no SMP, scheduler, preemption or task switching.

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

BoringKernel installs its own x86_64 Interrupt Descriptor Table and can diagnose fatal CPU exceptions without relying on a silent reset or missing serial output.

The verified fatal path is:

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

CPU exception vectors **0–31** are installed during `exception_init()`. The bootstrap hardware-IRQ layer subsequently installs DPL0 interrupt gates for legacy PIC vectors **32–47** into the same already loaded table. Entries 48–255 remain unconfigured.

All currently configured exception and IRQ descriptors use:

- present, DPL0 **64-bit interrupt gate** attributes `0x8e`;
- IST index 0;
- the kernel's currently executing CS selector, read at runtime rather than hard-coded;
- the corresponding architecture entry-stub address.

Before `lidt`, BoringKernel validates all 32 exception descriptors. The load helper executes `cli` before `lidt`. The kernel then executes `sidt` and rejects initialization unless the CPU-reported base and limit exactly match the requested IDTR. Hardware IRQ gates are added later while IF remains clear and are individually checked after installation.

### Exception names and vectors

Vectors 0–31 all have exception stubs. Architecturally defined exceptions receive descriptive names; reserved slots remain explicitly reserved. The current fatal policy does not attempt recovery or demand paging.

### C / assembly boundary

High-level exception policy remains in `kernel/arch/x86_64/exception.c`: IDT construction/validation, exception naming, diagnostic formatting, Page Fault decoding, Double Fault reporting and fatal-stop policy are C.

`kernel/arch/x86_64/exception_stubs.S` contains only x86_64 exception ABI mechanics: per-vector entry, hardware-error-code normalization, GPR preservation, the CPL0 trap-frame contract, descriptor-register instructions, reads of `CS`/`CR2`, and the deliberate hardware-fault triggers used by QEMU acceptance.

### Normalized trap frame

Exceptions with and without hardware error codes are normalized to one current CPL0 176-byte structure:

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

The current hardware-error-code exception set is vectors **8, 10, 11, 12, 13, 14, 17, 21, 29 and 30**. Other exception vectors receive a synthetic zero.

BoringKernel currently handles only CPL0 → CPL0 entries. x86_64 therefore does not push privilege-transition `RSP`/`SS`. The assembly computes the interrupted ring-0 RSP from the known frame depth and reads the current `SS` selector explicitly. When ring 3 is introduced later, this contract must be extended to distinguish CPU-pushed privilege-transition state.

### Fatal exception policy

Fatal exceptions print vector/name, normalized error code, RIP, CS, RFLAGS, RSP, SS and saved GPR state. Page Fault additionally reports `CR2` and the current low error-code interpretation. Fatal handling ends in the controlled `cli` plus infinite `hlt` path and does not return.

The dedicated QEMU tests continue to prove:

```text
Divide Error → CPU vector 0 → BoringKernel diagnostic → controlled halt
Page Fault   → CPU vector 14 → CR2/error decode → controlled halt
```

Double Fault still has no dedicated IST/emergency stack and therefore is not hardened against corruption of the ordinary kernel stack.

## Bootstrap hardware IRQ delivery

BoringKernel now has one deliberately small asynchronous hardware-interrupt path. Full details and the acceptance capture are in [`interrupts.md`](interrupts.md).

The verified path is:

```text
PIT channel 0
    ↓
IRQ0
    ↓
8259-compatible master PIC
    ↓
IDT vector 32
    ↓
x86_64 IRQ stub
    ↓
C IRQ dispatcher
    ↓
timer tick++
    ↓
PIC EOI
    ↓
restore + iretq
```

### PIC mapping and masks

The legacy PIC pair is initialized in 8086 mode and remapped so hardware IRQs cannot collide with CPU exceptions:

```text
master IRQ0–7  → vectors 32–39
slave IRQ8–15  → vectors 40–47
```

All sixteen IDT gates are installed, but all hardware IRQs begin masked:

```text
master = 0xff
slave  = 0xff
```

Only after PIT setup does BoringKernel unmask IRQ0:

```text
master = 0xfe
slave  = 0xff
```

No unrelated hardware IRQ is intentionally enabled.

For ordinary slave IRQs, EOI is sent to the slave before the master cascade. Ordinary master IRQs acknowledge the master only. Spurious IRQ7 and IRQ15 use ISR checks; spurious IRQ7 sends no EOI, while spurious IRQ15 acknowledges only the master cascade.

### PIT source

The timer uses PIT channel 0 in rate-generator mode. The bootstrap input clock is **1,193,182 Hz**. For a requested **100 Hz**, BoringKernel computes the nearest integer divisor **11932**, giving approximately **99.998491 Hz**. Serial output intentionally reports the rounded rate as **99998 mHz** rather than claiming exact 100 Hz timing.

### Interrupt enable point

IF remains clear throughout exception/IRQ-gate setup, PIC remapping, complete initial masking, PIT programming, IRQ0 unmasking and state validation. `irq_enable()` executes `sti` only after the expected masks `0xfe/0xff` are read back and the timer subsystem is ready.

The normal acceptance path verifies `timer_ticks()==0` before `sti`, then waits for at least ten asynchronous timer ticks with a bounded failure loop.

### IRQ frame and return

`kernel/arch/x86_64/irq_stubs.S` reuses the same current CPL0 176-byte trap-frame shape where technically clean. PIC interrupts have no CPU error code, so each stub supplies synthetic zero plus its vector, saves the GPRs, calls C dispatch, restores the interrupted state and executes `iretq`.

Unlike fatal exception entry, normal IRQ entry returns to interrupted kernel code. No stack switch, scheduler context, task object or preemption policy is present.

### Tick/concurrency assumptions

The periodic timer count is a `volatile uint64_t`, modified only by the IRQ0 handler and polled by ordinary code. This is sufficient only for the current single-CPU bootstrap assumptions; it is not a general SMP/thread-safety guarantee.

No dynamic memory allocation and no per-tick serial logging occurs in the IRQ handler.

### QEMU reference limitation

The current verified PIC/PIT path runs QEMU `q35` with a single `qemu64,apic=off` CPU. With QEMU's local APIC active, legacy PIC output is routed through LAPIC LINT0, and LAPIC configuration is intentionally outside this milestone. Disabling the CPU APIC feature keeps this proof genuinely PIC/PIT-only rather than silently adding LAPIC setup.

This is temporary bootstrap infrastructure, not the final interrupt architecture and not a physical-hardware compatibility claim. A later milestone may replace it with LAPIC/IOAPIC-based routing after that work is explicitly scoped.

### Acceptance proof

The verified normal QEMU run observed:

```text
Hardware interrupts:
Controller: 8259 PIC
PIC: remapped
Master vectors: 32-39
Slave vectors: 40-47
Initial master mask: 255
Initial slave mask: 255
IRQ0 vector: 32
Master mask: 254
Slave mask: 255

Timer:
Source: PIT channel 0
Input frequency: 1193182 Hz
Requested frequency: 100 Hz
Divisor: 11932
Effective frequency: 99998 mHz
IRQ: 0
Vector: 32
Timer: online

Interrupts: enabled

IRQ self-test:
  timer-delivery: PASS
  repeated-irqs: PASS
  acknowledgement: PASS
Ticks observed: 10
IRQ0 deliveries: 10
Unexpected IRQs: 0
Spurious IRQ7: 0
Spurious IRQ15: 0

BoringKernel hardware interrupt test passed.
```

Ten distinct deliveries are required specifically so a single initial IRQ cannot hide a missing EOI. The acceptance test never increments the tick counter manually or calls the IRQ dispatcher directly.

## Acceptance build modes

The Makefile retains three compile-time development modes:

```text
TEST_MODE=normal
TEST_MODE=divide
TEST_MODE=pagefault
```

`normal` runs PMM, VMM, heap and exception-infrastructure checks and then proves repeated real PIT/PIC hardware-IRQ delivery. `divide` and `pagefault` first pass the earlier subsystem checks and then intentionally trigger their respective real CPU faults. A mode-stamp dependency forces kernel objects to rebuild when the mode changes.

CI runs all three QEMU modes with finite timeouts. Missing diagnostics, wrong vectors, early QEMU termination, a stalled one-shot IRQ, failure markers or missing final success markers are not accepted as success.

## Still not implemented

BoringKernel does **not** yet own the complete address space. It still depends on Limine-created mappings and the Limine HHDM for the current execution environment and page-table access.

The kernel heap remains a small single-core bootstrap allocator, not a production allocator.

The exception subsystem remains fatal-diagnostic infrastructure, and the hardware-interrupt path is deliberately limited to a legacy PIC/PIT bootstrap timer. There is no LAPIC, IOAPIC, x2APIC, APIC timer, HPET, ACPI/MADT parsing, SMP, interrupt affinity, TSS/IST exception-stack hardening, scheduler, preemption, threads, context switching, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, BoringFS implementation, storage stack, keyboard/mouse driver, graphics, BoringWM, networking, USB, audio or GPU acceleration.

No scheduler code is part of the hardware-IRQ milestone.
