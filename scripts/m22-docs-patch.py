from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "docs/roadmap.md",
    "generic bounded block-device layer (Milestone 21 in progress)\n    ↓\nbackend operation table\n    ↓\nhardware storage driver (future Milestone 22)",
    "generic bounded block-device layer (Milestone 21 complete)\n    ↓\nmodern VirtIO PCI block backend (Milestone 22 in progress)\n    ↓\nreal QEMU raw disk I/O",
)
replace_once(
    "docs/roadmap.md",
    "There is still no numeric file-descriptor table, no stdin/stdout/stderr abstraction, no userspace file-content syscall API, no executable loading from VFS/RAMFS, no kernel BoringFS backend, no hardware storage driver, no networking, no display/input stack, no BoringWM integration, no APIC migration and no SMP.",
    "There is still no numeric file-descriptor table, no stdin/stdout/stderr abstraction, no userspace file-content syscall API, no executable loading from VFS/RAMFS, no kernel BoringFS backend, no partition layer, no persistent root filesystem, no networking, no display/input stack, no BoringWM integration, no APIC migration and no SMP.",
)

old_stage = """## Milestone 21: generic block-device layer — CURRENT / IN PROGRESS

Milestone 21 introduces a filesystem-independent bounded block-device interface with explicit logical geometry, a fixed registry, synchronous full-transfer read/write callbacks, overflow-safe range validation, read-only enforcement above the backend and explicit result codes.

The implementation is accepted only through the isolated `block` QEMU test mode, whose RAM-backed devices are test-only acceptance hardware. No hardware storage device is attached to QEMU and no test RAM device is registered during normal boot.

The architectural boundary is:

```text
future filesystem / storage consumer
              ↓
      block_device_read/write
              ↓
       generic block layer
              ↓
     backend operation table
              ↓
future hardware driver (Milestone 22)
```

## Milestone 22: QEMU VirtIO-block driver — PLANNED

Use VirtIO block as the first QEMU storage target unless implementation analysis finds a concrete blocker; prove bounded disposable-image read/write/read-back behavior before any persistent-root work.
"""
new_stage = """## Milestone 21: generic block-device layer — COMPLETE

Milestone 21 introduced the filesystem-independent bounded block-device interface with explicit logical geometry, a fixed registry, synchronous full-transfer read/write callbacks, overflow-safe range validation, read-only enforcement above the backend and explicit result codes.

Acceptance record:

```text
final PR: #31
frozen implementation: dc518ef24801a3b1e5d4b33387ad87ed97fc10f9
final closeout head: e63222865db5f0b4a837e0f40c9524214c430209
final PR CI: Run #182 / ID 32758293243 / SUCCESS
merged main: 8d50823ccf7d069ae9c861f644f74fe3e2c61ae4
merged-main CI: Run #183 / ID 32758477270 / push / main / 8d50823ccf7d069ae9c861f644f74fe3e2c61ae4 / SUCCESS
```

The isolated `block` QEMU test mode uses RAM-backed acceptance devices only. Milestone 21 itself did not add a hardware storage driver, BoringFS mount, partition layer, storage syscall, or FD layer.

## Milestone 22: QEMU VirtIO-block driver — CURRENT / IN PROGRESS

Milestone 22 adds the first real hardware-backed storage path: modern VirtIO 1.x PCI only, bounded PCI capability discovery, explicit cache-disabled MMIO mappings, PMM-owned DMA, a split virtqueue, one synchronous polling request at a time, and registration as `vblk0` beneath the existing M21 block-device API.

The disposable QEMU acceptance uses a deterministic raw disk and proves real host-authored reads, kernel writes, read-back, multi-request bounce-buffer chunking, neighbor preservation, and independent host-side persistence after QEMU exits.

Milestone 22 does not add a kernel BoringFS mount, partition layer, storage syscall, FD layer, storage interrupts, or asynchronous I/O. Milestone 23 remains planned and unstarted during M22 implementation.
"""
replace_once("docs/roadmap.md", old_stage, new_stage)

old_exact = """# Exact current implementation milestone

## Milestone 21 — generic block-device layer

The current implementation item is **Milestone 21**. It is **CURRENT / IN PROGRESS** in this reconciliation.

Its architectural boundary is only:

```text
future filesystem / storage consumer
              ↓
      generic block-device API
              ↓
    bounded validation / registry
              ↓
     backend operation table
```

The M21 acceptance RAM backend exists only under the isolated kernel test mode. Milestone 21 must not add VirtIO or PCI storage, attach a QEMU disk, parse partitions, mount BoringFS in the kernel, alter VFS/RAMFS, change the syscall ABI, introduce an FD layer or begin Milestone 22."""
new_exact = """# Exact current implementation milestone

## Milestone 22 — modern VirtIO PCI block device

The current implementation item is **Milestone 22**. It is **CURRENT / IN PROGRESS** in this reconciliation.

Its architectural boundary is:

```text
future filesystem / storage consumer
              ↓
      M21 generic block-device API
              ↓
       modern VirtIO block backend
              ↓
       modern VirtIO PCI transport
              ↓
         real QEMU raw disk
```

Milestone 22 must not mount BoringFS in the kernel, parse partitions, change VFS/RAMFS semantics, add a storage syscall or FD layer, introduce storage interrupts/async I/O, or begin Milestone 23."""
replace_once("docs/roadmap.md", old_exact, new_exact)

replace_once(
    "docs/virtio-block.md",
    "single-sector write/read-back, four-sector write/read-back, neighbor preservation, and M21 bounds rejection without queue submission.",
    "single-sector write/read-back, four-sector write/read-back, a twelve-sector M21 request that is split into 8+4-sector hardware requests, neighbor preservation, and M21 bounds rejection without queue submission.",
)
replace_once(
    "docs/virtio-block.md",
    "It verifies the exact bytes written by BoringKernel and independently verifies both neighboring sectors remain equal to their original deterministic pattern.",
    "It verifies the exact bytes written by BoringKernel, including the twelve-sector chunked write, and independently verifies both neighboring sectors remain equal to their original deterministic pattern.",
)
