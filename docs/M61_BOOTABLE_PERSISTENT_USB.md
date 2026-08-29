# M61 — Bootable + Persistent USB Root

M61 creates one bounded raw x86_64 UEFI USB image. The firmware and Limine boot from that image, and after BoringKernel initializes xHCI the same USB Mass Storage device is registered as `usb0` through the M60 BOT/SCSI path. BoringFS is mounted read/write through a bounded block-device slice of `usb0` and becomes the real VFS root for the native desktop session.

## Fixed image contract

The layout is defined once for the kernel in `kernel/include/boring/m61_usb_layout.h` and parsed by the host image builder.

- logical sector: 512 bytes
- raw image: 196608 sectors = 100663296 bytes (96 MiB)
- GPT metadata
- EFI System Partition: LBA 2048, 65536 sectors (32 MiB)
- BoringFS root slice: LBA 67584, 32768 sectors (16 MiB)
- remaining sectors intentionally unused except required GPT metadata

The EFI System Partition contains `EFI/BOOT/BOOTX64.EFI`, the pinned Limine 12.5.2 path, `boot/limine/limine.conf`, `boot/kernel.elf`, and the single boot module `boot/user/boring-init.elf`. Desktop children are not sourced from the EFI partition: they are loaded through VFS from BoringFS.

The kernel does not parse GPT in M61. It uses only the fixed, bounds-checked root slice contract. Invalid parent capacity, slice requests, or translated LBAs fail closed.

## Runtime root policy

M61 mode accepts exactly the existing descriptor-derived Mass Storage 08/06/50 path. It uses xHCI Bulk OUT/Bulk IN, BOT CBW/CSW, SCSI READ CAPACITY(10), READ(10), WRITE(10), and SYNCHRONIZE CACHE(10), then the M21 block layer `usb0`. If `usb0`, the fixed slice, or BoringFS cannot be established, M61 fails. It does not fall back to AHCI, VirtIO, RAMFS, or another device.

The desktop remains the established BoringOS chain: `boring-init` → `boring-display` → `boringwm` → `boring-terminal`. USB keyboard and tablet share the same xHCI controller while `usb0` is the active writable root.

## Build

Run:

```sh
sh scripts/build-m61-usb-image.sh
```

The output is `build/boringos-m61-usb.img`, with `build/boringos-m61-usb.img.sha256`, `build/boringos-m61-usb.img.xz`, and `build/m61-usb-image.txt`. Generated images are build artifacts and are not committed.

M61 automation never writes to a host block device. Physical flashing and the M59 Cthulhu validation remain a separate, manual user operation with an explicitly selected target device.
