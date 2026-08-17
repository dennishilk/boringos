# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine remains the external bootloader. After handoff, BoringKernel initializes COM1 serial output, consumes Limine's memory map for its 4096-byte physical page-frame allocator, controls selected x86_64 4-KiB virtual-to-physical mappings, provides a bounded dynamic kernel heap, and now installs its own x86_64 Interrupt Descriptor Table for CPU exception vectors 0–31.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.5-dev
Arch: x86_64
Hello from BoringKernel.
```

The VMM deliberately adopts the currently active Limine-created four-level page-table root rather than replacing the entire address space. Missing page-table frames come from PMM and physical page-table memory is accessed through Limine's reported HHDM.

The kernel heap reserves a finite 16-MiB virtual range, initially maps only two 4-KiB pages, grows one page at a time through PMM + VMM, uses deterministic first-fit allocation with 16-byte alignment and coalesces adjacent free blocks. Mapped heap pages are deliberately retained after `kfree` in this bootstrap stage.

BoringKernel now also owns a 256-entry x86_64 IDT with active DPL0 interrupt gates for exception vectors 0–31. The normal QEMU boot verifies the installed IDTR with `sidt`. Separate acceptance builds then trigger a **real CPU Divide Error** and a **real MMU Page Fault**, route both through BoringKernel's assembly entry stubs and C exception handler, print serial diagnostics, and stop through the controlled `cli`/`hlt` halt path. The Page Fault path reports `CR2` and decodes the relevant hardware error-code bits; it does not implement demand paging or repair the fault.

The automated QEMU acceptance suite therefore has three modes: normal boot/IDT initialization, real divide exception, and real Page Fault. The normal path continues to preserve and verify the existing PMM, VMM and heap tests.

BoringKernel therefore has **partial selected virtual-memory control, a functioning bounded bootstrap kernel heap, and a working fatal CPU-exception path through its own IDT**. It still does not own the complete address space, and its exception subsystem is intentionally minimal. Double Fault has a dedicated diagnostic path but still uses the ordinary kernel stack; no TSS/IST hardening exists yet.

This remains intentionally tiny. There is **no hardware IRQ routing, PIC/APIC/IOAPIC setup, timer, scheduler, userspace, filesystem, networking, graphical environment, input stack or process model yet**.

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

The full acceptance suite rebuilds BoringOS in its normal and deliberate fatal-test modes, boots QEMU headlessly, captures the serial console and verifies PMM, VMM, heap, IDT initialization, a real Divide Error and a real Page Fault:

```sh
make test
```

The exception modes are internal development/acceptance modes, not a runtime user-facing test framework.

See [`docs/architecture.md`](docs/architecture.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) and [`docs/boringfs.md`](docs/boringfs.md).

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
