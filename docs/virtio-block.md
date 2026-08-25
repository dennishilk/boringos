# Modern VirtIO block in BoringKernel

Milestone 22 adds the first real hardware-backed block-device path in BoringOS. The implementation is deliberately narrow: one modern VirtIO 1.x PCI block device under QEMU, one split virtqueue, one synchronous request at a time, and registration beneath the existing Milestone-21 generic block-device layer.

## Architecture

```text
consumer / M22 acceptance
        ↓
block_device_read / block_device_write
        ↓
M21 struct block_device_ops
        ↓
modern VirtIO block backend
        ↓
VirtIO 1.x PCI transport
        ↓
QEMU virtio-blk-pci
        ↓
raw disk image
```

The production block device is registered as `vblk0`. Its logical block size is 512 bytes because VirtIO block capacity is expressed in 512-byte sectors. The block count is the capacity reported by the device. Read-only state is propagated only when `VIRTIO_BLK_F_RO` is offered and accepted.

## Modern PCI transport only

The driver intentionally supports only the modern VirtIO PCI transport. Acceptance attaches QEMU with `virtio-blk-pci,...,disable-legacy=on`; there is no legacy I/O-BAR transport, transitional fallback, or platform-MMIO VirtIO implementation.

PCI discovery uses x86 Configuration Mechanism #1 (`0xCF8` / `0xCFC`) with bounded enumeration across buses, devices, and functions. The first supported modern VirtIO block device (`vendor 0x1AF4`, modern block device ID `0x1042`) is selected. No BDF is hard-coded.

The PCI command register is updated by preserving unrelated bits and enabling Memory Space and Bus Master before DMA is used.

## Capabilities and BARs

The PCI capability chain is walked with a fixed maximum, pointer alignment/range validation, cycle detection, and checked next pointers. The driver requires the VirtIO vendor capabilities for:

- common configuration;
- notification configuration;
- device-specific block configuration.

The notification capability's `notify_off_multiplier` is read from the real PCI capability. Memory BAR decoding supports both 32-bit and 64-bit memory BARs. Capability base plus offset/length arithmetic is checked before mapping.

## MMIO mapping

Normal `vmm_map_page()` keeps its existing PMM-backed-RAM contract. PCI MMIO is not made PMM-usable and that safety check is not removed.

Milestone 22 adds a separate bounded MMIO path using a fixed 16-MiB kernel virtual window beginning at `0xffffff0002000000`. MMIO leaf PTEs are explicitly writable and cache-disabled through x86 PCD. Existing VMM-owned page-table allocation and reclamation remain in use; only the leaf physical mapping is replaced with the PCI MMIO frame.

## DMA ownership

All device-visible queue/request memory is allocated as PMM frames. A small VMM helper converts a validated PMM frame to its existing HHDM address so drivers do not duplicate HHDM arithmetic.

The driver owns five PMM-backed DMA frames for kernel lifetime after successful initialization:

1. split-queue descriptor table;
2. available ring;
3. used ring;
4. request header plus status byte;
5. 4096-byte data bounce buffer.

No arbitrary heap pointer is treated as a DMA buffer and no IOMMU is required for this QEMU milestone.

## Device negotiation

Initialization follows the modern status sequence:

```text
reset to 0
ACKNOWLEDGE
DRIVER
feature negotiation
FEATURES_OK and verification
queue setup
DRIVER_OK and verification
```

Every hardware wait is bounded. Initialization fails cleanly on missing devices/capabilities, mapping failures, reset timeout, missing `VIRTIO_F_VERSION_1`, rejected features, unavailable queue, DMA failure, invalid geometry, or block-device registration failure. A fatal partial initialization marks FAILED where meaningful, resets the device, releases DMA frames, and unmaps MMIO before returning.

Only features the driver understands are accepted. `VIRTIO_F_VERSION_1` is mandatory; `VIRTIO_BLK_F_RO` is accepted when offered. Indirect descriptors, event index, packed rings, multiqueue, discard, write-zeroes, secure erase, topology, zoned block and SCSI passthrough are not negotiated.

## Split virtqueue

Only queue 0 is used. If the device exposes at least eight descriptors, the driver deliberately programs queue size 8.

Descriptor chains use exactly three entries:

```text
0: request header    device reads
1: data              device writes for READ, reads for WRITE
2: status byte       device writes
```

The queue's descriptor, driver/available, and device/used addresses are the actual PMM physical addresses. The notification address is calculated from the mapped notify capability plus `queue_notify_off * notify_off_multiplier`, with checked bounds.

Queue publication and completion use explicit x86 memory barriers. Completion is synchronous polling of the used-ring index with `pause` and a fixed spin limit; no storage interrupt, MSI, MSI-X, APIC, or asynchronous I/O path is introduced.

## Bounce buffering

The M21 API accepts arbitrary kernel buffers, which are not guaranteed physically contiguous. Therefore every request uses a PMM-backed 4096-byte bounce frame.

A single VirtIO request transfers at most eight 512-byte sectors. Larger M21 requests are split synchronously into at most eight-sector chunks. Only one request may be in flight; the same deterministic descriptor slots are reused while avail/used indices continue to advance and wrap normally.

## QEMU acceptance

`tests/virtio-block-qemu.sh` creates a fresh 2-MiB raw image containing deterministic non-zero sector patterns, builds the isolated `virtio-block` kernel test mode, and attaches the image explicitly as a modern-only `virtio-blk-pci` device.

The kernel proves discovery, status/feature negotiation, queue/DMA setup, M21 registration, a known host-authored sector read, first/last sector reads, single-sector write/read-back, four-sector write/read-back, neighbor preservation, and M21 bounds rejection without queue submission.

After the kernel success marker, QEMU is stopped and the host opens the same raw file directly. It verifies the exact bytes written by BoringKernel and independently verifies both neighboring sectors remain equal to their original deterministic pattern.

## Non-goals

Milestone 22 does not add partitions, GPT/MBR, a storage syscall, file descriptors, async I/O, multiqueue, hot-unplug, storage interrupts, APIC/MSI/MSI-X, IOMMU support, a persistent root filesystem, or userspace raw-disk access.

**No BoringFS mount exists in M22.** Kernel BoringFS integration remains Milestone 23 and is not started here.
