# BoringOS

[Deutsch](README.de.md)

BoringOS is an experimental **independent desktop operating system** for x86_64, built from scratch around its own kernel, **BoringKernel**.

It is **not Linux**, **not BSD**, and does not use another operating-system kernel. BoringOS-developed system components are written primarily in **C**, with small isolated x86_64 assembly where the architecture requires it.

> boring is not a bug.  
> it's the entire operating system now.

## Current status

~~~text
BoringKernel 0.0.62-dev
Milestone 61 · physical desktop milestone frozen
~~~

On **2026-09-04**, BoringOS crossed an important boundary on the real **Cthulhu** machine: the M61 USB image booted into a persistent native BoringWM desktop and accepted real USB keyboard input.

The last physically tested runtime candidate is:

~~~text
1e3c0e83e8e9159480782a6be624975ccbe0da3a
CI: 26 SUCCESS / 0 FAILURE
M61 raw image: 100663296 bytes
SHA256: 4ba4218ecafc04937737691f2133c8d8ed8b1bff29434048e2cdfb6f2a024947
~~~

The physical Cthulhu run now proves all of the following together:

- boot from the M61 UEFI USB image;
- real xHCI USB Mass Storage and writable BoringFS root;
- <code>boring-init</code>, <code>boring-display</code> and native C **BoringWM**;
- a persistent empty desktop that remains alive after the last window is closed;
- real USB keyboard input through BoringOS's own xHCI/HID path;
- physical <code>Super+Return</code>, <code>Super+E</code>, <code>Super+F</code> and <code>Super+Q</code>;
- multiple tiled native applications, close/reopen lifecycle and focus handling;
- **BoringTerminal**, **BoringEdit** and **BoringFiles** on real hardware;
- <code>boring-shell</code> and <code>boringfetch</code> running inside the physical graphical terminal;
- real CPUID, PCI and SMBIOS information for the Ryzen 7 5800X3D / B550 VISION D machine;
- real firmware framebuffer scanout on the monitor.

QEMU remains the complete automated regression platform, but the desktop itself is no longer only a QEMU claim.

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
processes + independent address spaces
        ↓
Ring 3 + native SYSCALL/SYSRETQ ABI
        ↓
VFS + BoringFS + block devices
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

Implemented foundations include physical/virtual memory management, a kernel heap, exceptions, PIC/PIT, cooperative and preemptive scheduling, process address spaces, Ring 3, native syscalls, ELF64 userspace, PTYs, file descriptors, VFS, BoringFS, native IPC, shared buffers, a display service, software composition, BoringWM, native applications, CPUID/PCI/SMBIOS inventory, xHCI HID, USB Mass Storage and AHCI/SATA storage.

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

The normal M61 desktop intentionally remains alive with **zero open windows**. Closing the final application returns to the empty BoringWM desktop; applications can then be launched again.

BoringTerminal runs a separately scheduled <code>boring-shell</code> through a real PTY. BoringEdit loads and saves through BoringFS. BoringFiles performs real VFS/BoringFS directory enumeration.

## Physical Cthulhu hardware

The current physical reference machine includes:

~~~text
CPU:       AMD Ryzen 7 5800X3D
Board:     Gigabyte B550 VISION D
Memory:    32 GiB installed, detected through SMBIOS
Firmware:  AMI / Gigabyte F18d
Display:   current firmware framebuffer 800x600x32, pitch 3328
~~~

Cthulhu exposes multiple xHCI controllers. Linux shows USB bus pairs behind at least <code>02:00.0</code>, <code>29:00.0</code> and <code>58:00.3</code>. The current BoringOS path still owns only one controller instance.

A directly attached Holtek USB keyboard on the active controller is physically proven through the whole path:

~~~text
xHCI Interrupt-IN
→ HID decode
→ BoringOS input queue
→ boring-display
→ BoringWM
→ native shortcuts / applications
~~~

## USB status and current boundary

The current USB stack supports xHCI controller bring-up, direct root-port device addressing, descriptor discovery, configuration, HID Interrupt-IN endpoints, HID keyboard/mouse formats used by the bounded path, and USB Mass Storage through Bulk/BOT/SCSI.

The next physical USB work is deliberately clear:

- support **multiple xHCI controllers** instead of only the first owned controller;
- add **USB hub enumeration**;
- then validate the physical mouse on its real topology;
- extend HID report support if a non-boot/report-descriptor device requires it.

The current ROCCAT mouse is not yet a BoringOS success claim. In the tested wiring it was behind a Genesys Logic hub, which BoringOS does not enumerate yet.

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

The AMD GPU is therefore involved in scanout, but BoringOS does **not** yet program the AMD display engine or submit GPU rendering commands itself.

There is currently no native AMD/NVIDIA/Intel modesetting or acceleration driver, no VRAM/GTT manager and no 2D/3D command submission. The immediate next graphics goal is a better/native GOP framebuffer mode, followed by faster software present and damage/dirty-region handling. A native AMD driver is a much later project.

## Current bootstrap limits

Some limits are intentionally small and now visible on real hardware:

- <code>KERNEL_PROCESS_MAX = 8</code>;
- <code>KERNEL_TASK_MAX = 8</code>;
- BoringWM currently supports 6 managed clients;
- roughly two fully functional terminals fit because each terminal also owns a separate shell process;
- the physical display is currently the firmware-provided 800x600 framebuffer;
- the PMM has a larger bounded design capacity, but full physical use of Cthulhu's installed 32 GiB is still future work;
- no USB hubs or multi-xHCI ownership yet;
- no networking, audio, NVMe, SMP runtime or native GPU driver yet.

These are implementation boundaries, not promises disguised as support.

## Storage and BoringFS

BoringOS has its own filesystem format, **BoringFS**. The repository contains the codec and validator, deterministic formatter, <code>boringfsck</code>, kernel mount support and writable operation.

Verified storage paths include VirtIO block, bounded synchronous AHCI/SATA, and M61's xHCI USB Mass Storage path. The physical M61 image boots and mounts its writable BoringFS root from the USB device.

## Hardware inventory

<code>boringfetch</code> reports kernel-owned hardware facts rather than invented platform strings:

~~~text
CPUID   → CPU vendor, brand, family/model/stepping
PCI     → BDF/vendor/device/class inventory
SMBIOS  → firmware, system, board and installed memory
INFO    → bounded versioned userspace snapshot
~~~

On Cthulhu this now runs inside the real graphical BoringTerminal.

## After M61

M61 is being frozen as the physical desktop baseline. The next work order is intentionally:

~~~text
M61 FREEZE
    ↓
1. process + task + desktop capacities
    ↓
2. USB: multi-xHCI + hubs + physical mouse
    ↓
3. native / better GOP resolution
    ↓
4. accelerate software graphics / present
    ↓
5. make useful physical use of the full 32 GiB RAM
    ↓
...
    ↓
eventually: a native AMD graphics driver
~~~

This is a direction list, not a claim that the later items are already implemented.

## Build and test

The build uses GCC/binutils as a freestanding x86_64 toolchain and a pinned Limine release.

~~~sh
make
make run
make test
~~~

The permanent CI suite keeps earlier milestone proofs alive. M61 additionally has exact-head USB-image, persistent-root, desktop, framebuffer, xHCI/HID and physical-observability acceptance.

The detailed historical source of truth remains [docs/roadmap.md](docs/roadmap.md).

## BoringWM

BoringOS contains its own native C BoringWM implementation for the BoringOS display and IPC stack.

The separate [dennishilk/boringwm](https://github.com/dennishilk/boringwm) repository is the original Rust/X11 project and a behavioral reference. It is not a BoringOS dependency.

## License

BoringOS is licensed under the [MIT License](LICENSE).
