# M58 — bounded high-memory PMM / >4 GiB physical memory

M58 removes the original ~4-GiB PMM development ceiling without replacing the
existing allocator architecture. BoringKernel remains deliberately bounded and
simple: 4-KiB frames, a static bitmap and deterministic allocation semantics.

## Implemented bound

- maximum managed frame count: 8,388,608
- frame size: 4 KiB
- configured PMM capacity: 32 GiB
- bitmap size: 8,388,608 bits = 1,048,576 bytes
- physical addresses, frame counters and byte counters remain 64-bit
- bitmap/frame indices and memory-map region arithmetic are checked before use

M58 adds `pmm_alloc_frame_in_range(minimum, maximum_exclusive, out)` as the
smallest generic constrained-allocation primitive needed for a bounded real
high-frame test and DMA-address limits. It is not a zone allocator, buddy
allocator or replacement PMM architecture.

## Real 32-GiB acceptance

Permanent workflow run `33258288429` boots real QEMU q35 with `-m 32G`. A shared
sparse file-backed main-RAM backend avoids the hosted runner's anonymous-memory
commit limit; it does not reduce guest-visible RAM.

Observed evidence from the guest:

```text
M58 PMM usable bytes: 34354872320
M58 PMM usable frames: 8387420
M58 high frame: 0x0000000100000000
M58 high frame >= 4GiB: PASS
M58 high frame write/read: PASS
M58 neighboring frame isolation: PASS
M58 high frame free: PASS
M58 accounting: PASS
M58 cleanup: PASS
M58 HIGH MEMORY TEST PASSED
```

The test allocates a real physical frame at or above 4 GiB without exhausting
millions of lower frames, accesses it through the established HHDM mapping,
checks deterministic first/last-byte patterns, verifies that an adjacent frame
is unchanged, frees both frames and requires PMM accounting to return exactly to
the pre-test state.

## DMA safety

AHCI keeps its established `CAP.S64A` contract. If S64A is present, DMA storage
may come from the full PMM range; otherwise AHCI requests frames below 4 GiB.
The existing xHCI path is conservatively kept below 4 GiB so a larger PMM cannot
silently feed it a high DMA address before 64-bit controller support is proven.
All programmed DMA address fields remain untruncated.

## Preserved boundaries

M58 does not add NUMA, huge pages, swapping, a page-cache redesign, SMP memory
policy, a new heap allocator, NVMe, USB mass storage, networking, partitioning or
physical-PC acceptance. The PMM is still statically bounded to 32 GiB rather than
being an arbitrary-scale enterprise allocator. SMBIOS installed memory and PMM
usable/free memory remain separate system-information concepts.

Runtime Semantic Freeze:

```text
SHA:  f6c6feff137f4ab36c41d26a5f14f7d6391dee19
tree: 70937b4b5b43c0e87ee016175302dbba03f62955
```

All 23 permanent feature-head workflows were SUCCESS at freeze, including the
USB-only graphical desktop, writable AHCI persistent root and complete Boot #668.
