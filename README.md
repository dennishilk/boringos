# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental **independent desktop operating system** for x86_64, built from scratch around its own kernel, **BoringKernel**.

It is **not Linux**, **not BSD**, and does not use another operating-system kernel. BoringOS-developed system components are written primarily in **C**, with small isolated x86_64 assembly where the architecture requires it.

> boring is not a bug.  
> it's the entire operating system now.

## Current status

~~~text
BoringKernel 0.0.62-dev
Milestone 63 · physical system-power baseline
~~~

On **2026-09-05**, BoringOS reached a new physical baseline on the real **Cthulhu** machine:

- the native BoringWM desktop boots from the writable USB image;
- real USB keyboard input works through BoringOS's own xHCI/HID path;
- BoringTerminal, BoringEdit and BoringFiles run on real hardware;
- dynamic process/task capacity is physically proven beyond the old eight-slot bootstrap ceiling;
- BoringFS creates and persists files on the physical USB root;
- `reboot` performs a real machine reset and BoringOS boots again;
- `shutdown` performs a real ACPI S5 power-off;
- data written before reboot survives the complete power lifecycle.

The physically accepted runtime is frozen at:

~~~text
freeze/m63-system-power-lifecycle-physical-2026-09-05
799d1e6529b8eafead37acc340f3fd18dbb2d655
~~~

Authoritative physical image:

~~~text
100663296 bytes
SHA256: 457535a2d27d98e489868a9d33cf8e8c2e2c83de13fc659bcea30e937c9018ab
~~~

QEMU remains the automated regression platform, but the desktop, persistent storage, reboot and shutdown paths are now also proven on real hardware.

## What runs today

~~~text
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
xHCI USB Mass Storage / AHCI / VirtIO
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
~~~

Implemented foundations include physical/virtual memory management, a kernel heap, exceptions, PIC/PIT, cooperative and preemptive scheduling, independent process address spaces, Ring 3, native syscalls, ELF64 userspace, PTYs, file descriptors, VFS, BoringFS, native IPC, shared buffers, a display service, software composition, BoringWM, native applications, CPUID/PCI/SMBIOS inventory, xHCI HID, USB Mass Storage, AHCI/SATA storage and ACPI system power control.

## Native desktop

The graphical desktop is entirely BoringOS-owned. There is no X11 or Wayland underneath it.

Current shortcuts:

~~~text
Super+Return   open BoringTerminal
Super+E        open BoringEdit
Super+F        open BoringFiles
Super+J/L      cycle focus
Super+Q        close focused managed client
~~~

The desktop intentionally remains alive with **zero open windows**. Closing the final application returns to the empty BoringWM desktop; applications can then be launched again.

BoringTerminal runs a separately scheduled `boring-shell` through a real PTY. BoringEdit loads and saves through BoringFS. BoringFiles performs real VFS/BoringFS directory enumeration.

## Shell and system lifecycle

The native shell currently includes filesystem, identity/process and power-lifecycle commands, including:

~~~text
ls cd pwd mkdir rmdir touch write rm
clear echo history help
uname hostname whoami ps
reboot shutdown
~~~

The M63 physical acceptance proved this complete sequence on Cthulhu:

~~~text
mkdir
touch
write
reboot
boot again
read persisted data
shutdown
~~~

`reboot` first synchronizes registered writable block devices and then enters a real platform reset path. `shutdown` synchronizes storage and enters ACPI S5. Neither command is implemented as a QEMU-only magic port.

## Process and desktop capacity

M62 removed the old static process/task arrays from the runtime architecture.

Current policy limits are:

~~~text
process policy limit: 64
task policy limit:    64
BoringWM clients:     16
WM IPC peers:         16
display IPC peers:    16
~~~

On Cthulhu the physical acceptance opened **seven BoringTerminal windows plus BoringEdit and BoringFiles**, closed everything, returned to the empty desktop and reopened applications without capacity or lifecycle failure.

These are policy bounds, not fixed architecture slots.

## Physical Cthulhu hardware

~~~text
CPU:       AMD Ryzen 7 5800X3D
Board:     Gigabyte B550 VISION D
Memory:    32 GiB installed, detected through SMBIOS
Firmware:  AMI / Gigabyte F18d
Display:   current firmware framebuffer 800x600x32, pitch 3328
~~~

Cthulhu exposes multiple xHCI controllers. The current runtime still owns only one controller instance.

A directly attached Holtek USB keyboard is physically proven through the full path:

~~~text
xHCI Interrupt-IN
→ HID decode
→ BoringOS input queue
→ boring-display
→ BoringWM
→ native shortcuts / applications
~~~

## USB and storage

BoringOS supports xHCI controller bring-up, direct root-port device addressing, descriptor discovery, HID Interrupt-IN and USB Mass Storage through Bulk/BOT/SCSI.

The physical USB root now has a strict durability path. Normal devices use SCSI `SYNCHRONIZE CACHE(10)`. If a device specifically reports that command as unsupported with the expected SCSI sense tuple, BoringOS switches writes to `WRITE(10)` with FUA instead of silently ignoring flush errors. Other transport, CSW, sense or FUA failures remain hard I/O errors.

The physical M63 acceptance proved writable BoringFS, persistence across reboot and a subsequent clean power-off on the real SanDisk USB device.

Other verified storage paths include VirtIO block and bounded synchronous AHCI/SATA.

## USB boundary / next hardware work

The next physical USB work is deliberately narrow:

1. support **multiple xHCI controllers** instead of only the first owned controller;
2. add **USB hub enumeration**;
3. validate the physical mouse through its real hub topology;
4. extend HID report support only if the real device requires it.

The current ROCCAT mouse is not yet a BoringOS success claim. In the tested wiring it sits behind a Genesys Logic hub, which BoringOS does not enumerate yet.

## Graphics today

BoringOS currently uses a **software-rendered framebuffer desktop**.

~~~text
BoringWM / boring-display composition in RAM
        ↓
CPU software present/copy
        ↓
firmware/Limine-provided framebuffer
        ↓
GPU scanout to the monitor
~~~

There is no native AMD/NVIDIA/Intel modesetting or acceleration driver yet. Near-term graphics work is a better/native GOP framebuffer mode followed by faster software present and damage/dirty-region handling. A native AMD driver is much later work.

## Current boundaries

- single owned xHCI controller;
- no USB hub enumeration yet;
- physical mouse behind the hub is not yet supported;
- physical display currently uses the firmware-provided 800x600 framebuffer;
- full practical use of Cthulhu's installed 32 GiB is still future work;
- no networking, audio, NVMe, SMP runtime or native GPU driver yet.

These are implementation boundaries, not promises disguised as support.

## Roadmap from the M63 freeze

~~~text
M63 PHYSICAL FREEZE
    ↓
1. USB multi-xHCI ownership
    ↓
2. USB hub enumeration + physical mouse
    ↓
3. better / native GOP resolution
    ↓
4. faster software graphics / present
    ↓
5. make useful physical use of the full 32 GiB RAM
    ↓
...
    ↓
eventually: native AMD graphics driver
~~~

The detailed historical record is in [docs/roadmap.md](docs/roadmap.md).

## Build and test

The build uses GCC/binutils as a freestanding x86_64 toolchain and a pinned Limine release.

~~~sh
make
make run
make test
~~~

The GitHub Actions workflows intentionally keep earlier milestone regressions alive. They are test coverage, not active development branches.

## Frozen physical baselines

The repository keeps a small number of immutable-by-policy physical freeze branches:

- `freeze/m61-physical-desktop-2026-09-04`
- `freeze/m62-dynamic-capacity-physical-2026-09-05`
- `freeze/m63-system-power-lifecycle-physical-2026-09-05`

Normal development continues from `main`; freeze branches are reference points and must not move.

## BoringWM

BoringOS contains its own native C BoringWM implementation for the BoringOS display and IPC stack.

The separate [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository is the original Rust/X11 project and a behavioral reference. It is not a BoringOS dependency.

## License

BoringOS is licensed under the [MIT License](LICENSE).
