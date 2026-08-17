# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine remains the external bootloader. BoringKernel currently provides COM1 serial output, a physical page-frame allocator, selected 4-KiB virtual mappings, a bounded dynamic kernel heap, its own x86_64 IDT, real CPU exception diagnostics, repeated PIT/PIC hardware IRQ0 delivery, cooperative and real hardware-timer-preemptive kernel tasks, and now a deliberately small **process identity plus independent address-space model**.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.9-dev
Arch: x86_64
Hello from BoringKernel.
```

The original VMM still adopts the active Limine-created x86_64 four-level root for PID 0. BoringKernel 0.0.9-dev additionally creates PMM-backed process roots with an empty private lower half and shared higher-half kernel mappings.

The current process split is:

```text
PML4 slots   0-255   process-private lower half
PML4 slots 256-511   shared kernel higher half
```

The shared higher half preserves the mappings needed for the kernel image, HHDM, heap, task stacks, PMM/VMM metadata, IDT, IRQ/exception code, scheduler state and still-required bootstrap structures. Shared page tables are never treated as process-owned frames and are never reclaimed by process destruction.

## Tasks and processes

A **task** is an execution/scheduling entity. A **process** is an identity plus address-space owner. They are intentionally separate concepts.

The bootstrap/kernel process is PID 0. The current acceptance test creates PID 1 and PID 2, each with a distinct PMM-backed root PML4. Process states remain deliberately small: `ALIVE` and `FINISHED`.

Each ordinary kernel task still receives an independent **16-KiB heap-backed stack**. Cooperative switching retains the small SysV AMD64 call-boundary context. Timer preemption retains the separate complete **192-byte** interrupt frame required to resume arbitrary integer execution state.

A task now references its owning process. When the scheduler selects a task owned by a different process, it activates that process root with a real CR3 load before returning the selected interrupt frame to assembly. The PIC EOI still occurs before assembly abandons the current IRQ stack.

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

This is **still CPL0-only**. Independent process address spaces do not imply userspace.

There is **no Ring 3, user CS/SS, TSS privilege-stack transition, syscall mechanism, userspace runtime, ELF loader, fork/exec/wait/signals, user-memory-copy API, VFS, storage stack, networking, graphical environment, input stack, SMP, PCID, copy-on-write, demand paging, swap, or FPU/SIMD context switching**.

The next execution-boundary work is a separately scoped real Ring 3 transition. It has not been started.

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

The full acceptance suite performs real QEMU boots for the normal kernel plus deliberate real Divide Error and Page Fault modes. It preserves all previous PMM, VMM, heap, IRQ, cooperative-task and timer-preemption checks and additionally verifies process creation, distinct roots, same-VA/different-PA isolation, real CR3 switching, kernel mapping continuity, PIT-preemptive address-space switching, bootstrap restoration and cleanup.

A successful normal run ends with:

```text
BoringKernel process/address-space test passed.
BoringKernel QEMU boot verification passed.
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
