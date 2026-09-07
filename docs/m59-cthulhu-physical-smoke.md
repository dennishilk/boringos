# M59 — Cthulhu physical-smoke boot readiness

M59 closes only the repository/QEMU side of the first bounded physical-PC smoke campaign. It does **not** claim that BoringOS has already booted successfully on the physical Cthulhu machine.

## Frozen runtime contract

The M59 smoke path reuses the established common boot foundations and then enters a dedicated diagnostic harness after PMM/VMM/heap, framebuffer, CPUID, PCI and SMBIOS initialization. The harness exercises the existing xHCI/HID stack and canonical input queue while deliberately avoiding normal block-root initialization.

The frozen path therefore proves these safety properties:

- no normal root selection or mount path is entered;
- no AHCI write path is initialized;
- no VirtIO root dependency is required;
- internal-storage writes remain explicitly `DISABLED`;
- xHCI keyboard and pointer input remain observable through the existing queue;
- the same bounded harness boots with normal RAM and a real 32-GiB QEMU memory map.

Runtime Semantic Freeze:

```text
SHA:  297b140260d2597df9be54b242a5361532168718
tree: af3844ee38221588bd593a3bba231db5d9a6932b
```

All 24 permanent feature-head workflows were terminal `SUCCESS` at freeze. The complete BoringKernel boot test was Run #680 / ID `33261514756` and the focused M59 run was ID `33261514849`.

## Focused QEMU evidence

The focused q35 acceptance uses `i8042=off`, `qemu-xhci`, `usb-kbd` and `usb-tablet`. Its 32-GiB run observed:

```text
Memory usable bytes: 34354835456
Memory above 4GiB: YES
Canonical input queued events: 2
Canonical input dropped events: 0
Storage writes: DISABLED
PHYSICAL SMOKE READY
M59 PHYSICAL SMOKE HARNESS PASSED
```

These values are evidence from that run, not hardware constants.

## UEFI USB image candidate

The exact frozen build was packaged as:

```text
boringos-m59-cthulhu-smoke.img
bytes: 5455872
sha256: 86d74a14c7ca885166b518d630d7d72b27cc21a81d0436b9f64add057b60ee06
artifact id: 9717400218
build commit: 297b140260d2597df9be54b242a5361532168718
```

OVMF successfully booted that image as read-only xHCI USB mass storage. Image flashing remains an explicit manual action with a user-verified target device; repository automation does not guess or write a host block device.

## Physical status

```text
M59 repository/QEMU readiness: PROVEN
M59 physical Cthulhu validation: PENDING USER HARDWARE TEST
```

The pending physical test does not block M60. A later user-run Cthulhu smoke may record physical evidence, but it must not silently move or rewrite this Runtime Semantic Freeze.

## Non-goals

M59 does not add USB mass storage, a USB block device, partition parsing, NVMe, networking, SMP, an installer, or physical-hardware success claims. USB mass storage begins only in M60 after M59 repository closeout and merged-main verification.
