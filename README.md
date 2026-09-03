# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental **independent desktop operating system** for x86_64, built from scratch around its own kernel, **BoringKernel**.

It is **not a Linux distribution**, **not BSD**, and **not based on another operating-system kernel**. BoringOS-developed system components are written primarily in **C**, with small isolated x86_64 assembly only where the architecture requires it.

> boring is not a bug.  
> it's the entire operating system now.

## Current status

The current main line is:

```text
BoringKernel 0.0.62-dev
```

Development is complete through **Milestone 61**.

BoringOS is no longer just an early boot-kernel: under QEMU it boots into a real native Ring-3 desktop session with its own display service, tiling window manager, graphical terminal, shell, editor, file manager, persistent BoringFS storage, hardware inventory, and a growing BoringOS-owned xHCI/USB stack.

It is still an experimental research/learning OS. QEMU remains the primary automated regression platform, but physical validation is no longer hypothetical: on **2026-09-03**, the exact M61 physical candidate `ded342e76a35d9c4558d17dda919575f5fe329ee` booted on the real **Cthulhu** machine from the M61 USB image and rendered the first intentional BoringOS framebuffer output on physical hardware. The board reached **POST 91**, proving that the first normal framebuffer store completed successfully.

The physical boot dashboard showed **CPU inventory, PCI inventory, SMBIOS, PMM, VMM, kernel heap, exceptions, input, IRQ, PIT, xHCI controller, USB addressing, USB descriptors, USB HID, USB mass storage, BoringFS persistent root, `boring-init`, `boring-display`, and BoringWM** as `[ OK ]`. The automatic terminal and final desktop-present stage are still pending physical completion, so a full physical desktop claim is intentionally not made yet.

The detailed source of truth is [`docs/roadmap.md`](docs/roadmap.md).

## First physical framebuffer output

The September 2026 Cthulhu run is the first confirmed intentional BoringOS image rendered on real hardware since the project began.

The physical candidate no longer relies on the inherited Limine framebuffer alias for normal scanout writes. It resolves the framebuffer aperture through the Limine HHDM/memory-map contract, validates the physical framebuffer range, maps a bounded writable cache-disabled kernel alias, and leaves the existing software rendering path unchanged. The frozen M61 breadcrumbs emit `90` immediately before the first normal framebuffer store and `91` only after that store returns; Cthulhu physically reached `91` while the boot dashboard was visible on the monitor.

This proves the real framebuffer path, not native GPU acceleration. BoringOS still uses firmware/Limine-provided scanout memory and software composition.

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
- bounded xHCI controller ownership, USB device addressing, descriptor discovery, `SET_CONFIGURATION`, HID Interrupt-IN endpoint setup, real Interrupt-IN transfer events, canonical input-queue integration and a complete i8042-free graphical-desktop proof through Milestone 54
- bounded 32-GiB PMM capacity with real QEMU memory above 4 GiB
- an M59 read-only physical-smoke boot path and exact UEFI USB image candidate with internal-storage writes disabled
- bounded xHCI USB Mass Storage Bulk/BOT/SCSI transport registered as `usb0`, with real read/write/cache-flush persistence and simultaneous USB HID coexistence through M60
- one bounded GPT/UEFI image that boots and mounts its fixed BoringFS slice from the same `usb0` device, with two-boot persistence and live HID coexistence through M61
- physical Cthulhu boot from that M61 USB path through real xHCI/USB Mass Storage, writable BoringFS root, `boring-init`, `boring-display`, BoringWM and the first confirmed physical BoringOS framebuffer output

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

`boringfetch` uses those kernel-owned values. In QEMU it therefore reports QEMU's emulated hardware. The Cthulhu physical path now also exercises the real CPU/PCI/SMBIOS collectors during boot; full physical `boringfetch` output still depends on completing the automatic-terminal stage.

Milestone 58 removes the old ~4-GiB PMM development ceiling for the bounded reference target. The existing PMM now has capacity for **8,388,608 4-KiB frames = 32 GiB**, and real QEMU `-m 32G` acceptance proves usable memory above 4 GiB plus a managed physical frame at `0x0000000100000000`. Maps beyond the configured 32-GiB PMM capacity remain explicitly bounded/capped rather than implying arbitrary-scale memory support.

Milestone 59 adds the bounded physical-smoke candidate: the exact image has been booted under OVMF as read-only xHCI USB mass storage while the guest exercises PMM/VMM/heap, framebuffer, hardware inventory and USB HID input without entering the normal block-root path. Internal-storage writes are explicitly disabled. Later M61 physical work has now surpassed that smoke target on Cthulhu: the machine boots through the real USB-root path, mounts writable BoringFS, starts the display stack and BoringWM, and renders the native boot dashboard through the normalized physical framebuffer mapping.

## xHCI / USB status

The modern USB path is now substantially beyond mere PCI detection.

Milestones 48–61 currently provide:

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
        ↓
canonical BoringOS input queue
        ↓
complete i8042-free graphical desktop acceptance
```

M59 reuses the HID path for a storage-write-disabled physical-smoke candidate. M60 adds one bounded directly attached USB Mass Storage device on the same xHCI controller: descriptor-derived class/subclass/protocol `08/06/50`, descriptor-derived Bulk IN/OUT endpoints, xHCI Bulk transfers, BOT CBW/CSW validation and a one-LUN SCSI subset covering INQUIRY, TEST UNIT READY, REQUEST SENSE fallback, READ CAPACITY(10), READ(10), WRITE(10) and SYNCHRONIZE CACHE(10). The device is registered through the existing M21 block-device API as `usb0`. Real q35 acceptance proves exact LBA-8 persistence with all bytes outside that sector unchanged while USB keyboard/tablet input remains live after Storage I/O. USB hubs, SuperSpeed companion semantics, BOT reset/stall recovery, filesystem mounting and root-from-USB remain outside M60.

M61 adds one fixed-layout 96-MiB GPT/UEFI image whose Limine boot files and writable BoringFS root live on the same xHCI USB device. Two consecutive QEMU boots of that exact image prove a flushed `/persist/m61.txt`, exact host-side bytes, terminal-visible recovery, live keyboard/tablet input, and unchanged protected boot regions. The physical Cthulhu run additionally proves that the real machine can traverse xHCI controller initialization, USB addressing, descriptor discovery, USB HID, USB Mass Storage and the BoringFS persistent-root mount from this path. This is a bounded USB-root image, not an installer or a general partition-discovery path.

## Storage and filesystems

BoringOS has its own small filesystem format, **BoringFS**. The repository contains the codec/validator, deterministic formatter, `boringfsck`, kernel mount support and synchronous writable operation.

The verified persistent QEMU root can use modern VirtIO PCI block storage, the bounded synchronous AHCI/SATA path completed in M57, or M61's fixed BoringFS slice on `usb0`. The AHCI and USB paths perform real reads, writes and required cache flushes through the generic block-device API; NVMe is not implemented.

On Cthulhu, the M61 physical boot dashboard now confirms the real USB Mass Storage path and persistent BoringFS root mount before the display stack and BoringWM start.

## Current boundaries

| Area | Current state |
| --- | --- |
| Primary verified platform | QEMU x86_64 for complete automated acceptance; physical Cthulhu is now verified through BoringWM and native framebuffer boot output |
| Physical hardware | M61 USB boot physically verified on Cthulhu through PMM/VMM, xHCI/HID/Mass Storage, BoringFS root, `boring-init`, `boring-display`, BoringWM and first real framebuffer output; automatic terminal/final desktop present still pending |
| Managed RAM | bounded 32-GiB PMM capacity; real 32-GiB QEMU maps and frames >= 4 GiB are verified |
| Graphics | firmware/Limine framebuffer + software compositor; real physical Cthulhu framebuffer output verified; no native AMD/NVIDIA/Intel GPU acceleration |
| Desktop input | xHCI USB HID path and legacy i8042/PS/2 path are both proven in bounded QEMU acceptances; xHCI HID initialization is also physically reached on Cthulhu |
| USB | xHCI HID plus bounded directly attached Bulk/BOT/SCSI Mass Storage and fixed-layout boot/root use through M61; the corresponding physical Cthulhu path is verified through persistent-root mount; hubs and SuperSpeed companion semantics are not implemented |
| Persistent root | VirtIO, AHCI/SATA, or M61's fixed `usb0` slice + BoringFS; the M61 `usb0` BoringFS root is now physically reached on Cthulhu |
| AHCI / NVMe | bounded synchronous AHCI read/write/flush; NVMe not implemented |
| Networking | not implemented |
| Audio | not implemented |
| SMP runtime | not implemented; current runtime remains deliberately bounded |
| POSIX compatibility | not a goal or current contract |
| Installer / polished live USB | not implemented; M61 publishes a bounded fixed-layout bootable/persistent USB image |

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

At this README revision, **M61 bootable persistent USB root is complete** and the active development banner is **BoringKernel 0.0.62-dev**. Physical M61 validation on Cthulhu is now confirmed through the real USB-root path, BoringWM startup and the first intentional native framebuffer output. The automatic terminal and final physical desktop-present stage remain unfinished. No M62 implementation is included.

See [`docs/roadmap.md`](docs/roadmap.md) for the exact current milestone state rather than relying on planned features in this README.

## BoringWM

BoringOS contains its own native C BoringWM implementation designed for the BoringOS display/IPC stack.

The separate [`dennishilk/boringwm`](https://github.com/dennishilk/boringwm) repository remains the original Rust/X11 project and a behavioral reference. It is **not** a BoringOS dependency or submodule.

## Principles

BoringOS prefers small modules, explicit interfaces, predictable behavior, readable C, bounded data structures, testable/auditable components, strict diagnostics, minimal dependencies, and accurate statements about what works and what does not.

The project should remain understandable by one determined developer.

## License

BoringOS is licensed under the [MIT License](LICENSE).
