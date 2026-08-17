# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine remains the external bootloader. After handoff, BoringKernel initializes COM1 serial output, consumes Limine's memory map for its 4096-byte physical page-frame allocator, controls selected x86_64 4-KiB virtual-to-physical mappings, provides a bounded dynamic kernel heap, owns an x86_64 IDT for CPU exception vectors 0–31, and can now receive and return from a real periodic legacy hardware timer interrupt.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.6-dev
Arch: x86_64
Hello from BoringKernel.
```

The VMM deliberately adopts the currently active Limine-created four-level page-table root rather than replacing the entire address space. Missing page-table frames come from PMM and physical page-table memory is accessed through Limine's reported HHDM.

The kernel heap reserves a finite 16-MiB virtual range, initially maps only two 4-KiB pages, grows one page at a time through PMM + VMM, uses deterministic first-fit allocation with 16-byte alignment and coalesces adjacent free blocks. Mapped heap pages are deliberately retained after `kfree` in this bootstrap stage.

BoringKernel owns a 256-entry x86_64 IDT. CPU exception vectors 0–31 remain DPL0 interrupt gates and the dedicated QEMU acceptance modes continue to prove a **real CPU Divide Error** and a **real MMU Page Fault** through BoringKernel's own entry stubs and C exception diagnostics.

The new bootstrap hardware-interrupt path installs gates for PIC vectors 32–47, remaps the legacy 8259 PIC to vectors 32–39 / 40–47, masks every PIC IRQ initially, programs PIT channel 0 for a requested 100 Hz rate, then unmasks only IRQ0. The normal QEMU test deliberately enables interrupts only after this state is validated and requires at least ten real IRQ0 deliveries. Each timer IRQ increments a tick counter, sends the required PIC End Of Interrupt and returns with `iretq`; no scheduler or task switching exists.

For this deliberately legacy PIC/PIT proof, the QEMU reference remains `q35` but uses a single bootstrap CPU with the local APIC feature explicitly disabled (`qemu64,apic=off`). This avoids silently introducing LAPIC configuration into a PIC-only milestone. PIC/PIT is temporary bootstrap infrastructure and is expected to be replaced by a modern APIC-based interrupt architecture later.

BoringKernel therefore has **partial selected virtual-memory control, a functioning bounded bootstrap kernel heap, a working fatal CPU-exception path, and a verified periodic hardware-IRQ path**. It still does not own the complete address space and remains single-core bootstrap software.

There is **no scheduler, preemption, threads, processes, ring 3, syscall layer, filesystem, storage stack, networking, graphical environment or input stack yet**. LAPIC, IOAPIC, HPET, ACPI/MADT and SMP are also not implemented.

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

The bootstrap QEMU command used by `make run` keeps `q35` while explicitly disabling the CPU's local APIC feature for the current PIC/PIT-only reference path.

The full acceptance suite rebuilds BoringOS in its normal and deliberate fatal-test modes. It verifies PMM, VMM, heap, IDT initialization, repeated real PIT/PIC IRQ0 delivery, a real Divide Error and a real Page Fault:

```sh
make test
```

The exception modes are internal development/acceptance modes, not a runtime user-facing test framework.

See [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) and [`docs/boringfs.md`](docs/boringfs.md).

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
