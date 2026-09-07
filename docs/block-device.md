# Generic block-device layer

Milestone 21 introduces the first filesystem-independent block I/O boundary in BoringKernel. It deliberately stops at a small synchronous device contract; filesystems and hardware drivers sit on opposite sides of that boundary and are not implemented by this milestone.

## Motivation

Storage consumers should not need to know whether a future device is backed by VirtIO, NVMe, AHCI, USB storage, or another driver. Conversely, a storage driver should not contain BoringFS-specific geometry or VFS policy. The generic layer gives both sides one bounded contract:

```text
future filesystem / storage consumer
              ↓
      block_device_read()
      block_device_write()
              ↓
       generic block layer
              ↓
     backend operation table
              ↓
       future hardware driver
```

No hardware storage driver exists in M21.

## Device descriptor

A registered `struct block_device` supplies:

- a stable non-empty name;
- a logical block size in bytes;
- a total logical block count;
- a read-only flag;
- an opaque backend context pointer;
- a mandatory synchronous read callback;
- a write callback for writable devices.

The generic API uses a 64-bit first-block index and an explicit 32-bit block count. It intentionally has no byte-offset API and contains no assumption that logical blocks are 4096 bytes. Devices with 512-byte and 4096-byte logical blocks are both valid.

The registry stores descriptor pointers. Registered descriptors, their names, operation tables, and backend contexts must therefore remain valid and semantically stable for the registry lifetime. Production drivers and the M21 acceptance backend use static-lifetime objects.

## Bounded registry

The registry is a fixed array of `BLOCK_DEVICE_MAX_DEVICES` entries. Milestone 21 sets that bound to eight devices. Registration order is deterministic, no heap allocation is required, the ninth device is rejected, and existing entries are never overwritten.

Registration rejects null or contradictory descriptors, including missing names or operations, a missing read callback, zero geometry, capacity multiplication overflow, duplicate device/name registration, and writable devices without a write callback.

## Read/write contract

`block_device_read()` and `block_device_write()` are synchronous full-transfer operations. `BLOCK_DEVICE_RESULT_OK` means all requested logical blocks were transferred. Any backend failure is returned to the caller as an explicit result; there is no partial-I/O count, asynchronous completion, request queue, scatter/gather list, DMA abstraction, or flush contract in M21.

A zero block count is an invalid argument rather than a silent no-op.

## Bounds policy

Every request is validated before backend dispatch. The generic layer requires:

```text
device is registered and valid
buffer != NULL
block_count > 0
first_block < total_blocks
block_count <= total_blocks - first_block
```

The subtraction form avoids an overflowing `first_block + block_count` check. Requests at the exact final valid boundary succeed; requests one block beyond it, very large first-block values, and very large counts are rejected before a driver callback can observe them.

## Read-only policy

Read-only policy is enforced above the backend. A write to a read-only device returns `BLOCK_DEVICE_RESULT_READ_ONLY` without invoking the backend write callback. This remains true even when the test backend exposes a write callback solely so acceptance can prove that the callback count does not change.

## Acceptance-only RAM backend

The M21 `block` test mode provides two small RAM-backed devices solely as acceptance hardware:

```text
testblk0    512-byte logical blocks   64 blocks   writable
testblk-ro  4096-byte logical blocks   8 blocks   read-only
```

The RAM-backed device exists only for acceptance testing. It is not RAMFS, not a persistent ramdisk feature, not exposed to userspace, and not registered during normal kernel boot.

The QEMU test proves registration and lookup, deterministic initial reads, single- and multi-block write/read-back, neighbor preservation, exact-end access, rejected-range non-dispatch, read-only non-dispatch, backend error propagation, and the fixed registry limit.

## M21 non-goals

Milestone 21 does not add:

- PCI discovery or any VirtIO code;
- a QEMU `-drive` storage device;
- NVMe, AHCI, USB-storage, DMA, queues, or storage IRQ handling;
- MBR/GPT or another partition layer;
- a BoringFS kernel mount or BoringFS parsing through this API;
- a VFS hook or mount type;
- storage syscalls or raw userspace disk access;
- a file-descriptor layer.

Milestone 22 is the first planned hardware storage driver and will implement QEMU VirtIO-block against this neutral operation contract. Milestone 23 may then consume a block device for a read-only BoringFS kernel mount, including any BoringFS-specific block-size compatibility checks at that higher layer.
