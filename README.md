# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine remains the external bootloader. After handoff, BoringKernel initializes COM1 serial output, consumes Limine's memory map for its 4096-byte physical page-frame allocator, controls selected x86_64 4-KiB virtual-to-physical mappings, provides a bounded dynamic kernel heap, owns an x86_64 IDT for CPU exceptions and bootstrap PIC IRQs, receives real periodic PIT timer interrupts, and can now deliberately switch between independent kernel execution contexts.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.7-dev
Arch: x86_64
Hello from BoringKernel.
```

The VMM deliberately adopts the currently active Limine-created four-level page-table root rather than replacing the entire address space. Missing page-table frames come from PMM and physical page-table memory is accessed through Limine's reported HHDM.

The kernel heap reserves a finite 16-MiB virtual range, initially maps only two 4-KiB pages, grows one page at a time through PMM + VMM, uses deterministic first-fit allocation with 16-byte alignment and coalesces adjacent free blocks. Mapped heap pages are deliberately retained after `kfree` in this bootstrap stage.

BoringKernel owns a 256-entry x86_64 IDT. CPU exception vectors 0–31 remain DPL0 interrupt gates and dedicated QEMU acceptance modes continue to prove a **real CPU Divide Error** and a **real MMU Page Fault** through BoringKernel's own entry stubs and C exception diagnostics.

The bootstrap hardware-interrupt path installs gates for PIC vectors 32–47, remaps the legacy 8259 PIC to vectors 32–39 / 40–47, masks every PIC IRQ initially, programs PIT channel 0 for a requested 100 Hz rate, then unmasks only IRQ0. The normal QEMU test requires repeated real IRQ0 delivery. Each timer IRQ increments a tick counter, sends the required PIC End Of Interrupt and returns with `iretq`.

BoringKernel now also has a deliberately small **cooperative kernel-task layer**. Each created task receives an independent 16-KiB heap-backed kernel stack and a minimal SysV AMD64 context containing `RSP`, `RBX`, `RBP` and `R12`–`R15`. A tiny architecture-specific routine switches those contexts only when kernel code explicitly calls `task_yield()`. New tasks enter through a C trampoline, normal task-function return marks them `FINISHED`, and the selector skips finished tasks before eventually restoring the saved bootstrap context.

The normal QEMU acceptance test creates two independent tasks and proves real alternating context switches, task-local stack state survival, explicit callee-saved register preservation, clean task return, stack cleanup and continued PIT tick progress. The accepted branch run observed **7 context switches** while timer ticks progressed from **10 to 16**.

This is **cooperative multitasking only**. The PIT interrupt handler does not invoke the task selector and does not perform a context switch. There is no timer-driven preemption or timeslice mechanism.

For the deliberately legacy PIC/PIT proof, the QEMU reference remains `q35` but uses a single bootstrap CPU with the local APIC feature explicitly disabled (`qemu64,apic=off`). This avoids silently introducing LAPIC configuration into the PIC-only bootstrap path. PIC/PIT is temporary infrastructure and is expected to be replaced by a modern APIC-based interrupt architecture later.

BoringKernel therefore has **partial selected virtual-memory control, a functioning bounded bootstrap kernel heap, a working fatal CPU-exception path, a verified periodic hardware-IRQ path, and verified cooperative switching between independent kernel execution contexts**. It still does not own the complete address space and remains single-core bootstrap software.

There is **no preemption, user process model, ring 3, syscall layer, filesystem, storage stack, networking, graphical environment or input stack yet**. LAPIC, IOAPIC, HPET, ACPI/MADT and SMP are also not implemented. The cooperative task layer has no sleeping/blocking states, priorities, synchronization primitives or FPU/SIMD context management.

## Engineering direction

BoringOS-developed system components will be written primarily in **C**. Minimal architecture-specific assembly may be used where it is technically unavoidable and must remain small, isolated, and documented.

> BoringOS is an independent operating system whose own system components are written primarily in C.

The first reference platform is **x86_64 under QEMU**. Broad physical-hardware compatibility is deliberately not an initial goal.

## Build and boot

The current build uses GCC/binutils as a freestanding x86_64 toolchain and downloads a pinned Limine release. Generated files stay under `build/`.

Required host tools include GNU Make, GCC/binutils, `curl`, `xorriso`, and QEMU for running/testing.

```sh
make
make run
```

The bootstrap QEMU command used by `make run` keeps `q35` while explicitly disabling the CPU's local APIC feature for the current PIC/PIT reference path.

The full acceptance suite rebuilds BoringOS in its normal and deliberate fatal-test modes. It verifies PMM, VMM, heap, IDT initialization, repeated real PIT/PIC IRQ0 delivery, cooperative kernel context switching, a real Divide Error and a real Page Fault:

```sh
make test
```

The exception modes are internal development/acceptance modes, not a runtime user-facing test framework.

See [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/tasks.md`](docs/tasks.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) and [`docs/boringfs.md`](docs/boringfs.md).

## Deliberately not early goals

The project will grow incrementally. Early milestones explicitly do **not** target networking, USB complexity, audio, advanced GPU acceleration, Wi-Fi/Bluetooth, suspend/resume, browsers, large compatibility layers or broad physical-PC hardware support.

Networking in particular is intentionally deferred until kernel/user separation, process isolation, separate address spaces, controlled system-call interfaces, validated kernel boundaries, and basic defensive-testing practices exist.

## Native desktop direction

BoringOS is not intended to require X11 or Wayland for its native desktop. A later milestone will define a deliberately small BoringOS-native display/window protocol and display service. The native BoringOS version of **BoringWM will be written in C**.

The existing [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository remains a separate Rust/X11 project and external behavioral reference. It is not a BoringOS code dependency or submodule. See [`docs/boringwm-reference.md`](docs/boringwm-reference.md).

## Repository layout

```text
boringos/
├── docs/
├── kernel/
├── user/
├── libs/
├── tests/
└── scripts/
```

## Principles

BoringOS should prefer small modules, explicit interfaces, predictable behavior, readable C, testable and auditable components, strict diagnostics, minimal dependencies, documented architecture decisions, and accurate reporting of what works and what does not.

The project should remain understandable by one determined developer.

## License

BoringOS is licensed under the [MIT License](LICENSE).
