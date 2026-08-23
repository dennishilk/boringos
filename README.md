# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine remains the external bootloader. BoringKernel currently provides COM1 serial output, a physical page-frame allocator, selected 4-KiB virtual mappings, a bounded dynamic kernel heap, its own x86_64 IDT, real CPU exception diagnostics, repeated PIT/PIC hardware IRQ0 delivery, cooperative and real hardware-timer-preemptive kernel tasks, a deliberately small process identity plus independent-address-space model, and a tightly controlled first **CPL0 -> CPL3 privilege transition**.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.10-dev
Arch: x86_64
Hello from BoringKernel.
```

The original VMM still adopts the active Limine-created x86_64 four-level root for PID 0. BoringKernel 0.0.9-dev introduced PMM-backed process roots with an empty private lower half and shared higher-half kernel mappings. BoringKernel 0.0.10-dev adds only the narrow descriptor, user-mapping and privilege-transition machinery needed for the first real Ring 3 acceptance path.

The current process split is:

```text
PML4 slots   0-255   process-private lower half
PML4 slots 256-511   shared kernel higher half
```

The shared higher half preserves the mappings needed for the kernel image, HHDM, heap, task stacks, PMM/VMM metadata, IDT, IRQ/exception code, scheduler state and still-required bootstrap structures. Shared page tables are never treated as process-owned frames and are never reclaimed by process destruction. The Ring 3 acceptance additionally verifies that all present shared PML4 entries in slots 256-511 remain supervisor-only (`U/S=0`).

## Tasks and processes

A **task** is an execution/scheduling entity. A **process** is an identity plus address-space owner. They are intentionally separate concepts.

The bootstrap/kernel process is PID 0. The process-address-space acceptance creates PID 1 and PID 2, each with a distinct PMM-backed root PML4. Process states remain deliberately small: `ALIVE` and `FINISHED`.

Each ordinary kernel task still receives an independent **16-KiB heap-backed stack**. Cooperative switching retains the small SysV AMD64 call-boundary context. Timer preemption retains the separate complete **192-byte** interrupt frame required to resume arbitrary integer execution state.

A task references its owning process. When the scheduler selects a task owned by a different process, it activates that process root with a real CR3 load before returning the selected interrupt frame to assembly. The PIC EOI still occurs before assembly abandons the current IRQ stack.

## Independent address-space proof

The central process-isolation address is:

```text
0x0000004000000000
```

The QEMU acceptance creates two process address spaces and maps the same VA to different physical frames:

```text
PID 1: TEST_VA -> frame A
PID 2: TEST_VA -> frame B
frame A != frame B
```

It then activates the real roots and dereferences `TEST_VA` through the CPU:

```text
PID 1 writes 0xAAAAAAAAAAAAAAAA
PID 2 writes 0xBBBBBBBBBBBBBBBB
PID 1 still reads 0xAAAAAAAAAAAAAAAA
PID 2 still reads 0xBBBBBBBBBBBBBBBB
```

The test does not fake isolation through HHDM physical aliases.

The stronger acceptance binds one CPU-bound preemptive task to each process. Neither task calls `task_yield()`. Real PIT IRQ0 scheduling alternates both the task and CR3, while each task repeatedly accesses the same virtual address and must see only its own pattern.

One verified clean-source QEMU run reported:

```text
Process A root:           0x0000000000078000
Process B root:           0x0000000000079000
Process A physical frame: 0x000000000007A000
Process B physical frame: 0x000000000007B000
Address-space switches:   18
Preemptive CR3 switches:   7
Process A slices:           3
Process B slices:           3
```

After the test BoringKernel restores PID 0/bootstrap CR3, frees the two task stacks, unmaps the private test pages, reclaims only process-owned page-table frames, frees the two data frames and verifies PMM/heap/VMM bookkeeping.

See [`docs/processes.md`](docs/processes.md) for the exact model and ownership rules.

## Current execution-model boundary

BoringKernel now has one deliberately constrained Ring 3 acceptance path; it does **not** yet have a general userspace execution environment.

The kernel installs its own GDT with kernel code/data descriptors, DPL3 user data/code descriptors and one 64-bit available TSS. The current selectors are:

```text
0x08  kernel code
0x10  kernel data
0x1B  user data, RPL3
0x23  user code, RPL3
0x28  TSS
```

The TSS owns a dedicated 16-KiB RSP0 stack. The Ring 3 test maps one fixed user code page at `0x0000000040000000` as present + user + read-only/executable, and one fixed user stack page at `0x0000000040010000` as present + user + writable. User access is propagated through every required PML4/PDPT/PD/PT level without making the shared higher-half kernel PML4 entries user-accessible.

The CPU enters CPL3 through a real `iretq` frame using CS `0x23`, SS `0x1B` and user RSP `0x0000000040011000`. The copied user payload records its live CS/RSP and writes to the user stack, then executes the privileged `cli` instruction. Running `cli` at CPL3 produces a real **#GP / vector 13**. The exception frame proves CPL3 origin and preserves the user RIP/RSP/SS, while the handler and frame themselves reside inside the dedicated TSS RSP0 kernel stack. The normalized C-facing user RSP/SS copies are checked against the actual hardware privilege-transition RSP/SS words.

There is still **no syscall mechanism, no `SYSCALL`/`SYSRET`, no userspace runtime, no ELF loader, no user-memory-copy API, no fork/exec/wait/signals, no VFS/storage stack, no networking, no graphical environment, no input stack, no SMP, no PCID, no copy-on-write, no demand paging, no swap, and no FPU/SIMD context switching**.

The next execution-boundary milestone after this Ring 3 work is a separately scoped **system-call boundary / syscall ABI**. It is not implemented here.

For the current bootstrap proof, QEMU remains:

```text
-M q35 -cpu qemu64,apic=off -m 128M
```

This is temporary bootstrap infrastructure and not a physical-hardware compatibility claim.

## Engineering direction

BoringOS-developed system components will be written primarily in **C**. Minimal architecture-specific assembly may be used where technically unavoidable and must remain small, isolated, and documented.

> BoringOS is an independent operating system whose own system components are written primarily in C.

The first reference platform is **x86_64 under QEMU**. Broad physical-hardware compatibility is deliberately not an initial goal.

## Build and test

The current build uses GCC/binutils as a freestanding x86_64 toolchain and downloads a pinned Limine release. Generated files remain under `build/`.

Required host tools include GNU Make, GCC/binutils, `curl`, `xorriso`, and QEMU.

```sh
make
make run
make test
```

The complete acceptance suite performs real QEMU boots for the normal kernel, deliberate real Divide Error and Page Fault modes, and the dedicated Ring 3 mode. It preserves all previous PMM, VMM, heap, IRQ, cooperative-task, timer-preemption, process/address-space and CR3-switching checks. The Ring 3 mode separately proves the GDT/TSS state, user-page permissions, supervisor-only shared higher half, real `iretq` entry to CPL3, real `cli`-generated #GP, hardware user RSP/SS preservation, and the TSS RSP0 kernel-stack transition.

A successful normal run ends with:

```text
BoringKernel process/address-space test passed.
BoringKernel QEMU boot verification passed.
```

A successful Ring 3 run ends with:

```text
BoringKernel Ring 3 test passed.
BoringKernel Ring 3 verification passed.
```

See [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/tasks.md`](docs/tasks.md), [`docs/processes.md`](docs/processes.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md), and [`docs/boringfs.md`](docs/boringfs.md).

## Native desktop direction

BoringOS is not intended to require X11 or Wayland for its native desktop. A later milestone will define a deliberately small BoringOS-native display/window protocol and service. The native BoringOS version of **BoringWM will be written in C**.

The existing [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository remains a separate Rust/X11 project and external behavioral reference. It is not a BoringOS code dependency or submodule.

## Principles

BoringOS should prefer small modules, explicit interfaces, predictable behavior, readable C, testable and auditable components, strict diagnostics, minimal dependencies, documented architecture decisions, and accurate reporting of what works and what does not.

The project should remain understandable by one determined developer.

## License

BoringOS is licensed under the [MIT License](LICENSE).
