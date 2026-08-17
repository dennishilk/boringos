# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine remains the external bootloader. After handoff, BoringKernel initializes COM1 serial output, consumes Limine's memory map for its 4096-byte physical page-frame allocator, and now also controls selected x86_64 4-KiB virtual-to-physical mappings.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.3-dev
Arch: x86_64
Hello from BoringKernel.
```

The VMM deliberately adopts the currently active Limine-created four-level page-table root rather than replacing the entire address space. It obtains missing page-table frames from the PMM, accesses physical page-table memory through Limine's reported HHDM, can map/translate/unmap ordinary 4-KiB kernel pages, and invalidates changed translations with `invlpg`.

The automated QEMU acceptance test verifies the existing PMM invariants and a real VMM mapping test that writes and reads data through a newly created virtual mapping before removing it and returning all test-owned frames.

BoringKernel therefore has **partial, selected virtual-memory control**, not complete address-space ownership. Kernel execution, the current stack, HHDM and boot structures still rely on mappings inherited from Limine.

This remains intentionally tiny. There is **no general-purpose kernel heap, userspace, scheduler, filesystem, networking, graphical environment, input stack, exception/interrupt subsystem or process model yet**.

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

The automated acceptance test rebuilds BoringOS, boots QEMU headlessly, captures the serial console and verifies boot identity, PMM and the selected VMM mapping self-test:

```sh
make test
```

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
