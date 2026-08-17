# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental independent desktop operating-system project.

It is **not a Linux distribution**, **not a BSD distribution**, and **not based on Redox or another existing operating-system kernel**. The project intends to develop its own kernel, **BoringKernel**, together with a native BoringOS userspace and, later, a native desktop stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extremely early / bootstrap stage.**

This repository currently establishes only the project foundation and documentation. It does **not** yet contain a working BoringKernel, bootable BoringOS image, native userspace, display server, package manager, or native BoringWM implementation. Planned functionality must not be described as implemented functionality.

## Engineering direction

BoringOS-developed system components will be written primarily in **C**. Minimal architecture-specific assembly may be used where it is technically unavoidable, for example during early CPU bring-up, interrupt entry/exit, context switching, or access to special CPU instructions. Such assembly should remain small, isolated, and documented.

A precise long-term description is:

> BoringOS is an independent operating system whose own system components are written primarily in C.

The first reference platform is **x86_64 under QEMU**. Broad physical-hardware compatibility is deliberately not an initial goal.

## First goal

The first development milestone is a small, genuinely bootable independent system:

```text
boot
  ↓
BoringKernel
  ↓
memory management
  ↓
scheduler / execution environment
  ↓
native BoringOS userspace
  ↓
boring-init
  ↓
boring-shell
```

The initial shell should eventually be able to report the system identity truthfully, for example:

```text
BoringOS 0.0.1-dev

Kernel:    BoringKernel
Arch:      x86_64
Userspace: BoringOS
Shell:     boring-shell
```

This is a target, not a claim about the current repository state.

## Deliberately not early goals

The project will grow incrementally. Early milestones explicitly do **not** target:

- networking
- USB complexity
- audio
- advanced GPU acceleration
- Wi-Fi or Bluetooth
- suspend/resume
- browsers
- large compatibility layers
- broad physical-PC hardware support

Networking in particular is intentionally deferred until kernel/user separation, process isolation, separate address spaces, controlled system-call interfaces, validated kernel boundaries, and basic defensive-testing practices exist.

## Native desktop direction

BoringOS is not intended to require X11 or Wayland for its native desktop. A later milestone will define a deliberately small BoringOS-native display/window protocol and display service, sufficient to support real graphical clients without reproducing an entire foreign display ecosystem.

A possible long-term direction is:

```text
applications
    ↓
boring-window protocol
    ↓
boring-display
    ↓
BoringWM
    ↓
framebuffer / graphics backend
```

The native BoringOS version of **BoringWM will be written in C**.

## Existing BoringWM

The existing [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository is a separate Rust/X11 project. It remains intact and is **not** a code dependency of BoringOS.

BoringOS will use it as a behavioral reference for concepts such as deterministic master/stack layout, workspaces, focus behavior, keyboard-first operation, client ordering, promotion to master, and intentionally small configuration. X11/EWMH-specific mechanics are not part of the native BoringOS contract.

See [`docs/boringwm-reference.md`](docs/boringwm-reference.md) for the bootstrap-stage reference analysis and integration recommendation.

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

These directories are intentionally mostly empty during the bootstrap stage. Their presence establishes boundaries; it does not imply implemented subsystems.

## Principles

BoringOS should prefer:

- small modules
- explicit interfaces
- predictable behavior
- readable C
- testable components
- auditable components
- strict diagnostics
- minimal dependencies
- documented architecture decisions
- accurate reporting of what works and what does not

The project should remain understandable by one determined developer.

## License

BoringOS is licensed under the [MIT License](LICENSE).
