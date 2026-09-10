# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental **independent desktop operating system** for x86_64, built from scratch around its own kernel, **BoringKernel**.

It is **not Linux**, **not BSD**, and does not use another operating-system kernel. BoringOS-developed system components are written primarily in **C**, with small isolated x86_64 assembly where the architecture requires it.

> boring is not a bug.  
> it's the entire operating system now.

## Current status

```text
BoringKernel 0.0.62-dev
M66 physical USB/HID baseline + physically proven low-latency cursor/focus path
```

The current physical foundation is proven on the real **Cthulhu** workstation. BoringOS boots its native writable USB system, owns the relevant xHCI controllers independently, enumerates the real Genesys Logic USB hub, accepts the downstream ROCCAT mouse through the native xHCI/HID path, keeps the direct Holtek USB keyboard working, and runs the native BoringWM desktop with BoringTerminal, BoringEdit and BoringFiles.

The authoritative M66 USB/HID recovery baseline remains frozen at:

```text
freeze/m66-physical-hid-recovery-2026-09-08
bd3f181d24547189b004328aea54c54fd194fc96
TREE: 96f5b177a5afa8ea24c9f94168117ce09c1c8507
Artifact: 10055706794
RAW: 100663296 bytes
SHA256: 83e22073a408fa45474b409804c4b29932b4451ce68ec87c8d21681d948858f8
```

### Physical cursor/focus responsiveness — accepted 2026-09-10

After the M66 baseline was stable, the remaining physical mouse problem was narrowed to software presentation rather than USB/HID. A normal pointer move previously forced a full 800x600 software compose and full framebuffer copy. Pointer-enter focus changes also synchronously sent unchanged client `CONFIGURE` messages; native clients answered with `COMMIT`, indirectly causing additional full-frame presents and briefly blocking later pointer movement.

The accepted fix keeps USB/HID, pointer semantics, GOP mode and resolution unchanged. Normal cursor movement now restores and presents only the bounded old/new 6x12 cursor regions. Pointer-generated focus changes ACK input first, update focus metadata atomically, avoid unchanged client `CONFIGURE` roundtrips, coalesce pending focus paints to the newest state, and present the bounded focus-border regions only when the input/IPC path is idle.

On physical Cthulhu this removed both observed latency modes: normal cursor motion is now as responsive as the user's Sway desktop, and continuously crossing between windows no longer makes the pointer stop at the boundary while waiting for the focus highlight.

The exact physically accepted runtime is frozen at:

```text
freeze/mouse-present-latency-physical-2026-09-10
c5ded88e3d473162b94f963d9509394246ef782c
TREE: 8a8c01fa61670f8085d8259c7635072d7317f36e
Artifact: 10154738768
RAW: 100663296 bytes
SHA256: 6ac9e6096849ea4e906f0c70a322a8599de163bec34499bdd8794f6b202ce26f
```

QEMU remains the automated regression platform, but the USB topology, hub-connected mouse, pointer focus and the low-latency cursor/focus presentation path are also physically proven on Cthulhu.

## What runs today

```text
UEFI / QEMU or physical Cthulhu
        ↓
      Limine
        ↓
   BoringKernel
        ↓
PMM / VMM / heap / IDT / scheduler
        ↓
dynamic process + task objects
        ↓
Ring 3 + native SYSCALL/SYSRETQ ABI
        ↓
VFS + writable BoringFS
        ↓
multi-xHCI / USB hubs / HID / USB Mass Storage
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

Implemented foundations include physical/virtual memory management, a kernel heap, exceptions, PIC/PIT, cooperative and preemptive scheduling, independent process address spaces, Ring 3, native syscalls, ELF64 userspace, PTYs, file descriptors, VFS, BoringFS, native IPC, shared buffers, a display service, software composition, BoringWM, native applications, CPUID/PCI/SMBIOS inventory, xHCI HID, USB Mass Storage, AHCI/SATA storage and ACPI system power control.

## Native desktop

The graphical desktop is entirely BoringOS-owned. There is no X11 or Wayland underneath it.

Current shortcuts:

```text
Super+Return   open BoringTerminal
Super+E        open BoringEdit
Super+F        open BoringFiles
Super+J/L      cycle focus
Super+Q        close focused managed client
```

The desktop intentionally remains alive with **zero open windows**. Closing the final application returns to the empty BoringWM desktop; applications can then be launched again.

BoringTerminal runs a separately scheduled `boring-shell` through a real PTY. BoringEdit loads and saves through BoringFS. BoringFiles performs real VFS/BoringFS directory enumeration.

## Shell and system lifecycle

The native shell includes filesystem, identity/process and power-lifecycle commands, including:

```text
ls cd pwd mkdir rmdir touch write rm
clear echo history help
uname hostname whoami ps
reboot shutdown
```

The M63 physical acceptance proved a real writable USB-root lifecycle on Cthulhu: create and write files, reboot, boot again, read the persisted data, and then cleanly power off through ACPI S5. `reboot` and `shutdown` synchronize registered writable block devices first; neither is implemented as a QEMU-only magic port.

## Process and desktop capacity

M62 removed the old static process/task arrays from the runtime architecture.

```text
process policy limit: 64
task policy limit:    64
BoringWM clients:     16
WM IPC peers:         16
display IPC peers:    16
```

On Cthulhu the physical acceptance opened **seven BoringTerminal windows plus BoringEdit and BoringFiles**, closed everything, returned to the empty desktop and reopened applications without capacity or lifecycle failure. These are policy bounds, not fixed architecture slots.

## Physical Cthulhu hardware

```text
CPU:       AMD Ryzen 7 5800X3D
Board:     Gigabyte B550 VISION D
Memory:    32 GiB installed, detected through SMBIOS
Firmware:  AMI / Gigabyte F18d
Display:   current firmware framebuffer 800x600x32, pitch 3328
```

The proven native input path is:

```text
xHCI controller
→ USB root / Genesys Logic hub
→ HID Interrupt-IN
→ BoringOS input queue
→ boring-display
→ BoringWM
→ native shortcuts / software cursor / pointer focus
```

## USB and storage

BoringOS supports bounded multi-xHCI controller ownership, root-port and downstream hub topology, descriptor discovery, HID Interrupt-IN and USB Mass Storage through Bulk/BOT/SCSI.

The physical USB root has a strict durability path. Normal devices use SCSI `SYNCHRONIZE CACHE(10)`. If a device specifically reports that command as unsupported with the expected SCSI sense tuple, BoringOS switches writes to `WRITE(10)` with FUA instead of silently ignoring flush errors. Other transport, CSW, sense or FUA failures remain hard I/O errors.

Other verified storage paths include VirtIO block and bounded synchronous AHCI/SATA.

## Graphics today

BoringOS currently uses a **software-rendered framebuffer desktop**.

```text
BoringWM / boring-display composition in RAM
        ↓
bounded cursor/focus region presents where applicable
or full software present for scene/layout/client changes
        ↓
firmware/Limine-provided framebuffer
        ↓
GPU scanout to the monitor
```

The physical cursor fast path now presents at most the clipped old/new 6x12 cursor rectangles instead of repainting all 480,000 pixels for every move. Focus-only pointer transitions use bounded border-region presentation and are deferred behind ready input/IPC work. Full scene changes, window geometry changes, wallpaper changes and client commits still retain the established full software-composition path.

There is no native AMD/NVIDIA/Intel modesetting or acceleration driver yet. A better/native GOP framebuffer mode is the next visible graphics target; broader generalized software damage/present work remains later, and a native AMD driver is much later work.

## Current boundaries

- held-key typematic repeat on the physical USB keyboard is still unresolved and intentionally parked for later input polish;
- the current keyboard mapping is still effectively ENG/US rather than a finished DE layout;
- the physical display currently uses the firmware-provided 800x600 framebuffer;
- full practical use of Cthulhu's installed 32 GiB is still future work;
- no networking, audio, NVMe, SMP runtime or native GPU driver yet.

These are implementation boundaries, not promises disguised as support.

## Roadmap from the M66 physical baseline

```text
M66 PHYSICAL USB/HID FREEZE
    ↓
low-latency cursor + pointer-focus path  ✅ physical Cthulhu proof
    ↓
better / native GOP resolution
    ↓
broader software graphics / damage / present
    ↓
make useful physical use of the full 32 GiB RAM
    ↓
networking / NVMe / audio / SMP
    ↓
eventually: native AMD graphics driver

parked input polish:
- held-key typematic
- proper DE keyboard layout
```

The detailed historical record is in [docs/roadmap.md](docs/roadmap.md).

## Build and test

The build uses GCC/binutils as a freestanding x86_64 toolchain and a pinned Limine release.

```sh
make
make run
make test
```

The GitHub Actions workflows intentionally keep earlier milestone regressions alive. They are test coverage, not active development branches.

## Frozen physical baselines

The repository keeps a small number of immutable-by-policy physical freeze branches:

- `freeze/m61-physical-desktop-2026-09-04`
- `freeze/m62-dynamic-capacity-physical-2026-09-05`
- `freeze/m63-system-power-lifecycle-physical-2026-09-05`
- `freeze/m66-physical-hid-recovery-2026-09-08`
- `freeze/mouse-present-latency-physical-2026-09-10`

Normal development continues from `main`; freeze branches are reference points and must not move.

## BoringWM

BoringOS contains its own native C BoringWM implementation for the BoringOS display and IPC stack.

The separate [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository is the original Rust/X11 project and a behavioral reference. It is not a BoringOS dependency.

## License

BoringOS is licensed under the [MIT License](LICENSE).
