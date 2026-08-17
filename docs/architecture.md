# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional.

- **Initial architecture:** x86_64.
- **Reference machine:** QEMU `q35`. For the current legacy PIC/PIT bootstrap proof, the single reference CPU is explicitly `qemu64,apic=off`; BoringKernel does not configure a LAPIC yet.
- **Bootloader:** Limine 12.5.2, used to load BoringKernel and provide boot-time information that is still needed by the current kernel, including the memory map, HHDM offset and paging mode.
- **Kernel format:** freestanding, statically linked ELF64 x86_64 (`kernel.elf`).
- **Kernel entry path:** firmware → Limine → ELF entry symbol `boring_kernel_entry` → BoringKernel C code.
- **C/assembly boundary:** kernel policy, allocation, exception diagnostics, IRQ dispatch and cooperative task selection remain in C. Tiny x86_64 inline assembly is isolated to CPU/I/O primitives such as port I/O, `CR3`, `invlpg`, IF control and the halt loop. `exception_stubs.S` contains exception ABI mechanics and deliberate fault triggers; `irq_stubs.S` contains hardware-IRQ entry/save/restore/`iretq` mechanics; `context_switch.S` contains only the minimal SysV AMD64 cooperative context save/restore boundary and its register-preservation acceptance probe.
- **Serial console:** legacy COM1 at I/O base `0x3f8`, configured for 115200 baud, 8 data bits, no parity, one stop bit; transmit is polled.
- **Privilege level:** the kernel currently executes at x86_64 CPL 0 (ring 0), as handed off by Limine. There is no ring-3/userspace execution yet.
- **Execution model:** one bootstrap CPU only. BoringKernel can switch cooperatively among independent kernel execution contexts when code explicitly calls `task_yield()`. There is no timer-driven preemption, SMP or user process model.

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

The allocator uses deterministic first-fit selection, 16-byte payload alignment, 48-byte in-heap block headers, splitting when a useful remainder exists and immediate coalescing of adjacent free blocks. `kmalloc(0)` returns `NULL`. Block topology, magic, guards, sizes, alignment, bounds and links are validated before allocator operations continue.

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

BoringKernel has one deliberately small asynchronous hardware-interrupt path. Full details and the IRQ acceptance capture are in [`interrupts.md`](interrupts.md).

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

Normal IRQ entry returns to interrupted kernel code. The IRQ dispatcher does not invoke the cooperative task selector and does not perform a context switch.

### Tick/concurrency assumptions

The periodic timer count is a `volatile uint64_t`, modified only by the IRQ0 handler and polled by ordinary code. This is sufficient only for the current single-CPU bootstrap assumptions; it is not a general SMP/thread-safety guarantee.

No dynamic memory allocation and no per-tick serial logging occurs in the IRQ handler.

### QEMU reference limitation

The current verified PIC/PIT path runs QEMU `q35` with a single `qemu64,apic=off` CPU. With QEMU's local APIC active, legacy PIC output is routed through LAPIC LINT0, and LAPIC configuration is intentionally outside this bootstrap path. Disabling the CPU APIC feature keeps this proof genuinely PIC/PIT-only rather than silently adding LAPIC setup.

This is temporary bootstrap infrastructure, not the final interrupt architecture and not a physical-hardware compatibility claim.

### IRQ acceptance proof

The verified normal QEMU path requires at least ten distinct IRQ0 deliveries specifically so a single initial interrupt cannot hide a missing EOI. The test never increments the tick counter manually or calls the IRQ dispatcher directly.

The accepted cooperative-context branch run reported the preceding timer proof as:

```text
IRQ self-test:
  timer-delivery: PASS
  repeated-irqs: PASS
  acknowledgement: PASS
Ticks observed: 10
IRQ0 deliveries: 10
Unexpected IRQs: 0

BoringKernel hardware interrupt test passed.
```

## Cooperative kernel execution contexts

Full task/context details are in [`tasks.md`](tasks.md).

The verified cooperative path is:

```text
bootstrap context
    ↓ explicit task_yield()
task A / independent stack A
    ↓ explicit task_yield()
task B / independent stack B
    ↓
task A resumes after its yield
    ↓
...
    ↓
FINISHED tasks are skipped
    ↓
bootstrap context resumes
```

This is genuine stack-pointer/context switching, not sequential calls disguised as multitasking.

### Minimal context contract

The current SysV AMD64 cooperative context contains only:

```text
RSP
RBX
RBP
R12
R13
R14
R15
```

`RBX`, `RBP` and `R12`–`R15` are the general-purpose callee-saved registers required to survive an ordinary SysV AMD64 C call. `RSP` identifies the suspended task stack. Caller-saved registers (`RAX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`–`R11`) are deliberately not promoted into this small context object.

There is no explicit `RIP` field. A suspended task's normal return address already lives on its saved stack, so `context_switch.S` restores the saved registers and `RSP`, then executes `ret`.

The acceptance probe explicitly loads known values into every saved callee-saved GPR, performs a real yield/context switch, and validates `RBX`, `RBP`, `R12`–`R15` plus `RSP` after that execution context resumes.

### Kernel task object and states

The internal task object owns:

- a monotonically assigned kernel task ID;
- one of exactly `READY`, `RUNNING`, `FINISHED`;
- the minimal x86_64 context;
- stack base and size;
- C entry function and argument;
- fixed-slot ownership state;
- the context's intended interrupt-enabled state.

Ordinary task metadata lives in a fixed four-slot table. This avoids speculative dynamic scheduler infrastructure. The current acceptance test creates exactly two ordinary tasks.

### Bootstrap context

The already-running kernel is represented as special task ID **0**. Its active Limine-provided stack is not copied. Its context becomes valid naturally when bootstrap first calls the architecture context-switch routine to leave that stack.

Bootstrap is not selected as an ordinary round-robin peer while runnable test tasks remain. When the final ordinary task returns and no `READY` task remains, the saved bootstrap context is restored and execution continues immediately after the original bootstrap `task_yield()` call.

### Task stacks and first entry

Every created ordinary task receives an independent **16 KiB** stack through the existing `kmalloc` kernel heap.

Current stack safety checks include 16-byte alignment, range/overlap checks, a low-end 64-bit sentinel, a reserved low 16-byte area and sentinel validation before cleanup. Guard pages are not implemented.

A never-run task has no natural suspended return frame. BoringKernel constructs a synthetic first frame at the aligned high end of the stack with the C `kernel_task_trampoline` address as the first return target. Loading that task context and executing `ret` enters the trampoline with the SysV AMD64 function-entry stack alignment expected by C.

The trampoline restores the task's intended interrupt state and calls its entry function. If the entry returns normally, the task is marked `FINISHED`; the selector never chooses it again. Another `READY` task is selected, or bootstrap is restored if none remains.

### Cooperative selector and interrupt policy

The selector is deterministic round-robin over fixed ordinary task slots and runs only when ordinary kernel code explicitly yields or when a task entry returns.

For the small critical section:

1. record whether the current context intended interrupts enabled;
2. execute `cli`;
3. update task state/current-task selection;
4. save the outgoing context and restore the incoming context;
5. once the incoming/resumed C context is coherent, restore that context's intended IF state.

This prevents PIT IRQ delivery halfway through partially changed stack/current-task state while ensuring interrupts do not remain globally disabled during normal task execution.

Crucially, PIT IRQ0 remains:

```text
IRQ0 → tick++ → PIC EOI → iretq
```

No IRQ0 path calls `task_yield()`, the selector or `x86_64_context_switch`. Therefore this milestone does **not** prove preemption.

### Completion and cleanup

Task entry return causes `RUNNING → FINISHED`. Finished tasks are not scheduled again and their currently active stack is never freed in place.

After both acceptance tasks have finished and bootstrap has resumed, the kernel validates each task stack sentinel and releases both 16-KiB stacks through `kfree`.

Because the existing heap intentionally retains mapped pages after free, task cleanup checks that heap allocation count and used bytes return to their previous values rather than falsely requiring heap page mappings to shrink.

### Cooperative acceptance proof

The verified branch QEMU run reported:

```text
Kernel tasks:
Mode: cooperative
Tasks created: 2
Task stack size: 16384 bytes
Bootstrap task ID: 0
Scheduler: online

Task A:
  iterations: 3
  local-state: PASS

Task B:
  iterations: 3
  local-state: PASS

Context switch self-test:
  task-a-start: PASS
  task-b-start: PASS
  alternating-switch: PASS
  stack-isolation: PASS
  register-state: PASS
  task-return: PASS
  timer-coexistence: PASS
  stack-cleanup: PASS
  heap-bookkeeping: PASS
Context switches: 7
Ticks before task test: 10
Ticks after task test: 16
Task stacks freed: 2
Task heap allocations after cleanup: 0

BoringKernel cooperative task test passed.
```

Each task keeps a local counter on its own stack across yields. The test also proves the two local addresses are distinct and belong to the currently active task stack. Seven switches match the expected bootstrap → A/B alternating path and clean return to bootstrap.

Timer progress from 10 to 16 ticks proves that cooperative context switching did not leave hardware interrupts disabled. It does not select or preempt tasks.

## Acceptance build modes

The Makefile retains three compile-time development modes:

```text
TEST_MODE=normal
TEST_MODE=divide
TEST_MODE=pagefault
```

`normal` runs PMM, VMM, heap and exception-infrastructure checks, proves repeated real PIT/PIC hardware-IRQ delivery, then runs the cooperative two-task context-switch acceptance test. `divide` and `pagefault` first pass the earlier memory/exception setup and then intentionally trigger their respective real CPU faults. A mode-stamp dependency forces kernel objects to rebuild when the mode changes.

CI runs all three QEMU modes with finite timeouts. Missing diagnostics, wrong vectors, early QEMU termination, stalled IRQ/task switching, failure markers or missing final success markers are not accepted as success.

## Still not implemented

BoringKernel does **not** yet own the complete address space. It still depends on Limine-created mappings and the Limine HHDM for the current execution environment and page-table access.

The kernel heap remains a small single-core bootstrap allocator, not a production allocator.

The exception subsystem remains fatal-diagnostic infrastructure, and the hardware-interrupt path remains deliberately limited to a legacy PIC/PIT bootstrap timer.

The cooperative task layer is also deliberately narrow. It does **not** provide timer-driven preemption, timeslices, sleeping/blocking/waiting states, wakeups, priorities, fairness accounting, synchronization primitives, FPU/SIMD context switching, guard pages, per-process address spaces, user processes or a stable scheduler API.

There is still no LAPIC, IOAPIC, x2APIC, APIC timer, HPET, ACPI/MADT parsing, SMP, interrupt affinity, hardened TSS/IST exception stacks, ring 3, syscalls, userspace loader, VFS, RAMFS, BoringFS implementation, storage stack, keyboard/mouse driver, graphics, BoringWM, networking, USB, audio or GPU acceleration.

The exact next execution-model blocker, only when separately requested, is timer-driven preemption: connecting a timer interrupt to a safe scheduling/context-switch boundary without weakening the cooperative context, IRQ acknowledgement and exception invariants already proven.
