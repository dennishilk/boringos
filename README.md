# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental **independent desktop operating system** for x86_64, built from scratch around its own kernel, **BoringKernel**.

It is **not a Linux distribution**, **not BSD**, and **not based on another operating-system kernel**. BoringOS-developed system components are written primarily in **C**, with small isolated x86_64 assembly only where the architecture requires it.

> boring is not a bug.  
> it's the entire operating system now.

## Current status

The current main line is:

```text
BoringKernel 0.0.59-dev
```

Development is complete through **Milestone 58**.

BoringOS is no longer just an early boot kernel: under QEMU it now boots into a real native Ring-3 desktop session with its own display service, tiling window manager, graphical terminal, shell, editor, file manager, persistent BoringFS storage, hardware inventory, and a growing BoringOS-owned xHCI/USB stack.

It is still an experimental research/learning OS. QEMU is the primary verified platform. Physical-PC boot readiness is being developed deliberately and is **not yet a physical-hardware success claim**.

The detailed source of truth is [`docs/roadmap.md`](docs/roadmap.md).

## What runs today

```text
QEMU x86_64 / UEFI or BIOS path
        ↓
      Limine
        ↓
   BoringKernel
        ↓
PMM / VMM / heap / IDT / scheduler
        ↓
processes + independent address spaces
        ↓
Ring 3 + native SYSCALL/SYSRETQ ABI
        ↓
VFS + RAMFS + VirtIO/AHCI block + BoringFS
        ↓
     boring-init
        ↓
  boring-display
        ↓
     BoringWM
        ↓
┌──────────────┬──────────────┬──────────────┐
│BoringTerminal│ BoringEdit   │ BoringFiles  │
│ boring-shell │              │              │
│ boringfetch  │              │              │
└──────────────┴──────────────┴──────────────┘
```

Implemented and verified foundations include:

- bounded physical and virtual memory management plus a kernel heap
- x86_64 exceptions, legacy PIC/PIT interrupts, cooperative and timer-driven preemptive scheduling
- process identity, independent page-table roots, CR3 switching, CPL3 and TSS/RSP0 transitions
- native x86_64 `SYSCALL` / `SYSRETQ`
- validated static ELF64 userspace loading and a BoringOS-owned freestanding C runtime
- VFS, mutable RAMFS, writable persistent BoringFS, generic block I/O, VirtIO and bounded synchronous AHCI/SATA storage
- PID 1 `boring-init`, native `boring-shell`, process lifecycle, PTYs, file descriptors/stdio and scheduler-owned `SPAWN`
- anonymous Ring-3 memory, a small userspace heap, shared buffers, native service registry and IPC
- framebuffer ownership/presentation, native `boring-display`, software composition and cursor
- native C **BoringWM** with bounded tiling, focus, reordering and lifecycle handling
- native graphical **BoringTerminal**, **BoringEdit**, **BoringFiles**, `/bin/boringfetch` and `/bin/cat`
- real CPUID, PCI and SMBIOS platform inventory exposed honestly through `boringfetch`
- bounded xHCI controller ownership, USB device addressing, descriptor discovery, `SET_CONFIGURATION`, HID Interrupt-IN endpoint setup, real Interrupt-IN transfer events and bounded HID report decoding through Milestone 52

## Native desktop

The graphical desktop is entirely BoringOS-owned. It does not use X11 or Wayland.

Current keyboard shortcuts include:

```text
Super+Return   open BoringTerminal
Super+E        open BoringEdit
Super+F        open BoringFiles
Super+J/L      cycle focus
Super+Q        close focused managed client
```

`BoringTerminal` runs a separately scheduled `boring-shell` through a real PTY. Multiple graphical terminals have independent processes, address spaces and input focus. The terminal can run real standalone BoringFS executables such as `boringfetch` and `cat`.

`BoringEdit` is a bounded native text editor with real BoringFS load/save support. `BoringFiles` is a native file manager backed by real VFS directory enumeration and can open files in BoringEdit.

The desktop startup path is real too: PID 1 starts the display server and BoringWM from BoringFS and supervises the bounded desktop session rather than relying on extra boot modules for each component.

## Hardware inventory and real-PC direction

BoringOS reads real guest hardware data instead of filling the UI with invented machine names. Current collectors include:

```text
CPUID   → vendor, brand, family/model/stepping and bounded feature data
PCI     → real BDF/vendor/device/class inventory
SMBIOS  → firmware, system, board and memory identity
INFO    → bounded versioned userspace snapshot
```

`boringfetch` uses those kernel-owned values. In QEMU it therefore reports QEMU's emulated hardware; on physical hardware the goal is to report the machine's actual identity.

Milestone 58 removes the old ~4-GiB PMM development ceiling for the bounded reference target. The existing PMM now has capacity for **8,388,608 4-KiB frames = 32 GiB**, and real QEMU `-m 32G` acceptance proves usable memory above 4 GiB plus a managed physical frame at `0x0000000100000000`. Maps beyond the configured 32-GiB PMM capacity remain explicitly bounded/capped rather than implying arbitrary-scale memory support.

The first physical-PC target remains a bounded UEFI bring-up: Limine → BoringKernel → firmware framebuffer → hardware inventory → native desktop. Internal disks must not be treated as writable until an explicitly supported storage path exists.

## xHCI / USB status

The modern USB path is now substantially beyond mere PCI detection.

Milestones 48–52 currently provide:

```text
xHCI PCI discovery / BAR + MMIO validation
        ↓
legacy ownership handoff / halt / reset / start
        ↓
DCBAA + command ring + event ring + ERST
        ↓
real root-port connect state
        ↓
Enable Slot + Address Device
        ↓
per-device EP0 transfer ring
        ↓
real GET_DESCRIPTOR control-IN
        ↓
Device + Configuration descriptor validation
        ↓
SET_CONFIGURATION
        ↓
descriptor-derived HID Interrupt-IN endpoint contexts
        ↓
Configure Endpoint
        ↓
PMM-owned report DMA + Normal TRBs
        ↓
real Transfer Events + bounded HID report decoding
```

The current boundary is deliberate: **M52 receives and decodes real USB keyboard and QEMU absolute-tablet reports only after genuine xHCI Transfer Events, but it does not yet publish those decoded reports into the normal BoringOS input queue.** The existing graphical desktop therefore still relies on the proven legacy i8042/PS/2 input path for normal interaction.

There is still no USB hub support or USB mass-storage driver.

## Storage and filesystems

BoringOS has its own small filesystem format, **BoringFS**. The repository contains the codec/validator, deterministic formatter, `boringfsck`, kernel mount support and synchronous writable operation.

The verified persistent QEMU root can use modern VirtIO PCI block storage or the bounded synchronous AHCI/SATA path completed in M57. The AHCI path performs real reads, writes and required cache flushes through the generic block-device API; NVMe is not implemented.

## Current boundaries

| Area | Current state |
| --- | --- |
| Primary verified platform | QEMU x86_64 |
| Physical hardware | readiness candidate only; not physically verified |
| Managed RAM | bounded 32-GiB PMM capacity; real 32-GiB QEMU maps and frames >= 4 GiB are verified |
| Graphics | firmware/Limine framebuffer + software compositor; no native AMD/NVIDIA/Intel GPU acceleration |
| Desktop input | native i8042/PS/2 path |
| USB | xHCI devices addressed/configured; real HID Interrupt-IN report transport + bounded decode proven; input-queue integration still pending |
| Persistent root | VirtIO or AHCI/SATA block + BoringFS |
| AHCI / NVMe | bounded synchronous AHCI read/write/flush; NVMe not implemented |
| Networking | not implemented |
| Audio | not implemented |
| SMP runtime | not implemented; current runtime remains deliberately bounded |
| POSIX compatibility | not a goal or current contract |
| Installer / polished live USB | not implemented |

These limits are intentional. BoringOS tries to report unsupported hardware and incomplete subsystems honestly rather than turning detection into a support claim.

## Syscall ABI

BoringOS currently uses one project-owned provisional x86_64 syscall ABI. It has grown from the original `GETPID`/debug boundary into bounded console, filesystem, process, descriptor, input, memory, shared-buffer, IPC, framebuffer, event, PTY and process-spawn operations.

The current ABI occupies syscall numbers **0–43**. It is deliberately **not a stable POSIX ABI**. See [`docs/syscalls.md`](docs/syscalls.md) and [`docs/roadmap.md`](docs/roadmap.md) for the exact contract.

## Build and test

The build uses GCC/binutils as a freestanding x86_64 toolchain and a pinned Limine release. Generated files stay under `build/`.

Typical host requirements include GNU Make, GCC/binutils, `curl`, `xorriso`, and QEMU.

```sh
make
make run
make test
```

`make run` is the historical/headless bootstrap path, not the best demonstration of the complete graphical desktop. The graphical milestones use dedicated QEMU acceptance/bundle paths; see [`docs/RUNNING-M36.md`](docs/RUNNING-M36.md) and the current milestone records in [`docs/roadmap.md`](docs/roadmap.md).

The permanent CI suite keeps earlier milestone proofs alive while new capabilities are added. Tests include host/model checks, sanitizers where appropriate, real QEMU hardware paths, process/resource cleanup and exact framebuffer evidence for graphical milestones.

## Roadmap

The project advances in small semantic milestones. A milestone is not considered complete merely because code builds: focused acceptance, inherited regressions, a Semantic Freeze, runtime-neutral version closeout, exact-head CI, guarded squash merge and merged-main verification are part of the development discipline.

At this README revision, **M58 is complete** and the active development banner is **BoringKernel 0.0.59-dev**. M59 is not part of this closeout; the exact next milestone remains defined by the roadmap rather than being started here.

See [`docs/roadmap.md`](docs/roadmap.md) for the exact current milestone state rather than relying on planned features in this README.

## BoringWM

BoringOS contains its own native C BoringWM implementation designed for the BoringOS display/IPC stack.

The separate [`dennishilk/boringwm`](https://github.com/dennishilk/boringwm) repository remains the original Rust/X11 project and a behavioral reference. It is **not** a BoringOS dependency or submodule.

## Principles

BoringOS prefers small modules, explicit interfaces, predictable behavior, readable C, bounded data structures, testable/auditable components, strict diagnostics, minimal dependencies, and accurate statements about what works and what does not.

The project should remain understandable by one determined developer.

## License

BoringOS is licensed under the [MIT License](LICENSE).
