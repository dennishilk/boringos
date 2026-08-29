# M56 — synchronous read-only AHCI SATA block path

## Scope

M56 extends the completed M55 AHCI/SATA controller foundation with the smallest technically correct synchronous read-only ATA data path and publishes the discovered SATA device through the existing M21 generic block-device layer.

M56 must reuse the existing PCI inventory, MMIO mapping, PMM/VMM infrastructure and generic block-device API. It must not create a second storage abstraction.

The bounded implementation may add only what is required for one synchronous request at a time:

- PMM-owned, physically addressed AHCI command list storage for the selected SATA port
- PMM-owned received-FIS storage
- PMM-owned command-table storage
- bounded PRDT construction with checked size/address arithmetic and AHCI-required alignment
- bounded port stop/start sequencing around command-list/FIS programming
- IDENTIFY DEVICE through the real AHCI controller
- parsing of real device capacity, logical sector size and LBA capability from IDENTIFY data
- a synchronous read-only block callback using the smallest correct SATA DMA read command for the discovered device (READ DMA EXT when 48-bit LBA is available)
- bounded polling of PxCI, PxTFD and PxIS with timeout/error handling
- out-of-range and transfer-bound rejection before touching hardware
- registration of the real SATA disk below the existing M21 generic block-device layer as read-only
- deterministic q35 + real AHCI + RAW-SATA-image acceptance proving reads through the generic block-device API

## Acceptance

The focused M56 QEMU test must use a deterministic RAW SATA image with known sector patterns and prove through the real AHCI path:

1. IDENTIFY DEVICE completes and reports real capacity / logical sector size / LBA capability.
2. The first valid region is read correctly.
3. A middle region is read correctly.
4. The last valid region is read correctly without crossing device capacity.
5. A multi-sector read is correct.
6. An out-of-range request is rejected before command submission.
7. PRDT / transfer-size bounds are enforced.
8. Read-only operation leaves neighboring sectors unchanged in the host RAW image.
9. The device is visible through the existing M21 block-device layer, not a parallel API.

## Strict non-goals

M56 does not add writes, flush/persistence semantics, a writable AHCI device, BoringFS root over AHCI, partition parsing (MBR/GPT), NCQ, hotplug, port multipliers, ATAPI, asynchronous or interrupt-driven AHCI I/O, NVMe, networking, or M57 work.

M57 persistent writable AHCI root work is explicitly out of scope until M56 is Semantic Frozen, runtime-neutrally closed, guarded-squash merged, and fully green on merged main.
