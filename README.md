# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental **independent desktop operating system** for x86_64, built from scratch around its own kernel, **BoringKernel**.

It is **not Linux**, **not BSD**, and does not use another operating-system kernel. BoringOS-developed system components are written primarily in **C**, with small isolated x86_64 assembly where the architecture requires it.

> boring is not a bug.  
> it's the entire operating system now.

## Current status

~~~text
BoringKernel 0.0.62-dev
Milestone 66 · physical multi-xHCI + USB hub + mouse baseline
~~~

On **2026-09-06**, BoringOS reached a new physical USB baseline on the real **Cthulhu** machine:

- the native BoringWM desktop still boots from the writable USB image;
- BoringOS owns the relevant xHCI controllers independently instead of assuming one global controller;
- the real Genesys Logic USB hub is enumerated through bounded hub-class power, status and reset handling;
- the real ROCCAT mouse behind that hub reaches BoringOS through the native xHCI/HID path;
- physical mouse movement drives the existing software cursor and changes BoringWM focus between two live windows as the pointer crosses them;
- the real USB keyboard remains functional through the same boot;
- the previously frozen M63 persistent BoringFS, reboot and ACPI S5 lifecycle remains the storage/power baseline.

The physically accepted runtime is frozen at:

~~~text
freeze/m66-usb-hub-mouse-physical-2026-09-06
8ccd618dfc4e8163821de552a3c912bab4e6f36a
~~~

Authoritative physical image:

~~~text
100663296 bytes
SHA256: 84dfb521c2359364ba2f3f78718b686638d81f3d07f35050f32e5a2f91bd0c61
Artifact: 9987432260
~~~

QEMU remains the automated regression platform, but multi-controller xHCI ownership, real hub enumeration and the hub-connected physical mouse path are now also proven on Cthulhu. M63 remains the accepted physical durability/reboot/shutdown baseline.

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

Cthulhu exposes multiple xHCI controllers, and the M66 physical baseline now owns the relevant controller instances independently.

The direct Holtek USB keyboard and the ROCCAT mouse behind the real Genesys Logic hub are physically proven through the native path:

~~~text
xHCI controller
→ USB root / hub topology
→ HID Interrupt-IN
→ BoringOS input queue
→ boring-display
→ BoringWM
→ native shortcuts / cursor / pointer focus
~~~

## USB and storage

BoringOS supports bounded multi-xHCI controller ownership, root-port and downstream hub topology, descriptor discovery, HID Interrupt-IN and USB Mass Storage through Bulk/BOT/SCSI. The M66 physical boot proves the real Genesys Logic hub and its downstream ROCCAT mouse on Cthulhu.

The physical USB root now has a strict durability path. Normal devices use SCSI `SYNCHRONIZE CACHE(10)`. If a device specifically reports that command as unsupported with the expected SCSI sense tuple, BoringOS switches writes to `WRITE(10)` with FUA instead of silently ignoring flush errors. Other transport, CSW, sense or FUA failures remain hard I/O errors.

The physical M63 acceptance proved writable BoringFS, persistence across reboot and a subsequent clean power-off on the real SanDisk USB device.

Other verified storage paths include VirtIO block and bounded synchronous AHCI/SATA.

## USB physical status / next input polish

The former USB-topology blocker is closed physically:

1. **multiple xHCI controllers** are owned independently;
2. the **Genesys Logic hub** is enumerated on real hardware;
3. the downstream **ROCCAT mouse** moves the BoringOS cursor;
4. BoringWM pointer hit-testing physically changes focus between two windows.

The next observed input rough edge is keyboard typematic repeat: a held key such as Backspace currently produces only the initial action on the USB boot-keyboard path, so repeated deletion still requires repeated key presses. This is a small input-polish task, not a USB topology regression.

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

- held-key typematic repeat is not implemented yet on the USB keyboard path;
- physical display currently uses the firmware-provided 800x600 framebuffer;
- full practical use of Cthulhu's installed 32 GiB is still future work;
- no networking, audio, NVMe, SMP runtime or native GPU driver yet.

These are implementation boundaries, not promises disguised as support.

## Roadmap from the M66 freeze

~~~text
M66 PHYSICAL FREEZE
    ↓
1. keyboard typematic / held-key repeat
    ↓
2. better / native GOP resolution
    ↓
3. faster software graphics / present
    ↓
4. make useful physical use of the full 32 GiB RAM
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
- `freeze/m66-usb-hub-mouse-physical-2026-09-06`

Normal development continues from `main`; freeze branches are reference points and must not move.

## BoringWM

BoringOS contains its own native C BoringWM implementation for the BoringOS display and IPC stack.

The separate [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository is the original Rust/X11 project and a behavioral reference. It is not a BoringOS dependency.

## License

BoringOS is licensed under the [MIT License](LICENSE).
