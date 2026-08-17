# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on another existing operating-system kernel**. BoringOS develops its own kernel, **BoringKernel**, together with a future native BoringOS userspace and desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early bootstrap kernel.**

BoringKernel boots under **QEMU x86_64**. Limine is used only as the external bootloader; after handoff, execution reaches the freestanding BoringKernel entry point. BoringKernel initializes COM1 serial output and now consumes Limine's memory map to provide a minimal 4096-byte physical page-frame allocator with allocate/free support and an in-kernel QEMU self-test.

Current serial output begins with:

```text
BoringOS booting...
BoringKernel 0.0.2-dev
Arch: x86_64
Hello from BoringKernel.
```

The boot acceptance test also verifies PMM initialization, runtime-discovered usable-memory/frame counts, unique aligned usable-frame allocations, free/reuse behavior and allocator bookkeeping.

This remains intentionally tiny. There is **no general-purpose kernel heap, BoringKernel-owned virtual-memory manager, userspace, scheduler, filesystem, networking, graphical environment, input stack, interrupt subsystem or process model yet**.

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

The automated acceptance test rebuilds BoringOS, boots QEMU headlessly, captures the serial console and verifies both the boot identity and the physical-memory self-test:

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
