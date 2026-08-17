# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine remains the external bootloader. BoringKernel currently provides COM1 serial output, a physical page-frame allocator, selected 4-KiB virtual mappings, a bounded dynamic kernel heap, its own x86_64 IDT, real CPU exception diagnostics, repeated PIT/PIC hardware IRQ0 delivery, cooperative kernel contexts, and now **real hardware-timer-driven preemptive switching between independent kernel tasks**.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.8-dev
Arch: x86_64
Hello from BoringKernel.
```

The VMM deliberately adopts the active Limine-created x86_64 four-level page-table root rather than replacing the entire address space. BoringKernel therefore has selected virtual-memory control but does not yet own a complete independent address space.

The bounded kernel heap reserves a finite 16-MiB virtual range, grows through PMM + VMM, uses deterministic first-fit allocation with 16-byte alignment, and coalesces adjacent free blocks. Mapped heap pages are currently retained after `kfree`.

BoringKernel owns a 256-entry x86_64 IDT. Dedicated QEMU modes continue to prove a **real CPU Divide Error** and a **real MMU Page Fault** through BoringKernel's own exception entry path and controlled fatal diagnostics.

The bootstrap hardware path remaps the legacy 8259 PIC to vectors 32–47, programs PIT channel 0 for a requested 100 Hz rate, and unmasks only IRQ0. The actual divisor is 11932, approximately 99.998491 Hz; serial output reports `99998 mHz` rather than claiming exact timing.

## Kernel tasks

Each ordinary kernel task receives an independent **16-KiB heap-backed stack**. Task state remains deliberately small:

```text
READY
RUNNING
FINISHED
```

BoringKernel supports two distinct switching boundaries.

### Cooperative switching

Explicit `task_yield()` uses the small SysV AMD64 call-boundary context:

```text
RSP RBX RBP R12 R13 R14 R15
```

The existing QEMU acceptance test still proves alternating cooperative execution, task-local stack state, callee-saved register preservation, clean task return, stack cleanup, bootstrap return, and continued PIT tick progress.

### Timer-driven preemption

Preemption is a separate interrupt-time mechanism. A PIT IRQ may arrive at an arbitrary instruction, so the IRQ path preserves the complete integer register and interrupt-return state on the interrupted task's own stack.

The current normalized x86_64 preemption frame is **192 bytes** and contains:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
vector error-code
RIP CS RFLAGS RSP SS
```

The scheduler policy is intentionally simple:

```text
deterministic round-robin
1 PIT tick = 1 quantum
```

When preemption is enabled, IRQ0 advances the timer, enters the scheduler, retains the interrupted frame on the current task's stack, selects the next READY task, sends PIC EOI **before abandoning the current IRQ stack**, and returns the selected frame pointer to assembly. The assembly restore path then switches `RSP`, restores the full integer state, and resumes the selected task through `iretq`.

Fresh preemptive tasks receive a deliberate synthetic IRQ/`iretq` frame on their own stack and enter the same C task trampoline used for normal task-entry handling. The bootstrap kernel stack is not copied: its real PIT interrupt frame is retained and later restored so normal initialization continues at the interrupted bootstrap instruction stream.

The verified CPU-bound preemption test contains **no `task_yield()` call in either stress task**. One accepted QEMU branch run reported:

```text
Timer ticks during test: 7
Scheduler ticks: 7
Preemptions: 7
Task A slices: 3
Task B slices: 3
Task A resumes: 2
Task B resumes: 2
Cooperative yields during test: 0
```

The same test verifies task-local counters/checksums, independent stack addresses, stack sentinels, finished-task skipping, bootstrap return, cleanup, heap bookkeeping, and an assembly-assisted **full integer GPR preservation probe** across genuine hardware-timer preemption.

Kernel C remains built with implicit x87/MMX/SSE/SSE2 generation disabled. There is no FPU/SIMD task-state switching yet.

## Current execution-model boundary

This is **kernel-task preemption only**. All current tasks run at CPL0 in the same kernel address space.

There is still **no user process model, ring 3, syscall layer, separate process address spaces, CR3 task switching, userspace loader, filesystem, storage stack, networking, graphical environment, or input stack**. There is also no sleeping/blocking scheduler, wait queues, priorities, realtime policy, synchronization framework, SMP scheduler, LAPIC/IOAPIC timer architecture, or FPU/SIMD context switching.

For the current legacy timer proof, QEMU remains:

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
```

The full acceptance suite rebuilds BoringOS in the normal and deliberate fatal-test modes. It verifies PMM, VMM, heap, IDT, repeated real PIT/PIC IRQ0 delivery, cooperative kernel switching, timer-driven preemptive kernel scheduling, a real Divide Error, and a real Page Fault:

```sh
make test
```

See [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/tasks.md`](docs/tasks.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md), and [`docs/boringfs.md`](docs/boringfs.md).

## Native desktop direction

BoringOS is not intended to require X11 or Wayland for its native desktop. A later milestone will define a deliberately small BoringOS-native display/window protocol and service. The native BoringOS version of **BoringWM will be written in C**.

The existing [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository remains a separate Rust/X11 project and external behavioral reference. It is not a BoringOS code dependency or submodule.

## Principles

BoringOS should prefer small modules, explicit interfaces, predictable behavior, readable C, testable and auditable components, strict diagnostics, minimal dependencies, documented architecture decisions, and accurate reporting of what works and what does not.

The project should remain understandable by one determined developer.

## License

BoringOS is licensed under the [MIT License](LICENSE).
