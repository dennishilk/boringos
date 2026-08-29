# M55 — Bounded AHCI / SATA Controller Foundation

Status: scope anchor; runtime implementation follows on this branch.

M55 adds only the hardware-controller foundation for one segment-zero PCI SATA AHCI controller. It does not add a sector data path.

## Architecture boundary

The storage architecture remains:

`AHCI hardware backend → existing M21 generic block-device layer → existing BoringFS → existing VFS → persistent root/userspace`

M55 reuses the M44 PCI inventory for controller selection and the existing PMM/VMM/MMIO infrastructure. It does not create a second PCI enumeration, block API, filesystem path, or input/desktop stack.

## Runtime scope

The implementation is bounded to:

- select exactly one real PCI device classified as class `0x01`, subclass `0x06`, programming interface `0x01` from the existing inventory;
- obtain and validate the controller ABAR from PCI configuration space without hard-coded BDF/vendor/device/port values;
- reject I/O, unsupported, zero, malformed, misaligned or overflowing MMIO ranges;
- map the AHCI register window through the existing cache-disabled MMIO mapper and roll it back on partial failure;
- capture `CAP`, `CAP2` when architecturally available, `VS`, `GHC`, `PI`, and bounded BIOS/OS handoff state;
- bound all ownership/engine polls by explicit iteration limits;
- inspect only ports allowed simultaneously by the architectural port limit, `CAP.NP`, `PI`, and the explicit BoringOS limit;
- read `PxCMD`, `PxSSTS`, `PxSIG`, and `PxTFD`, defensively classify DET/IPM, and distinguish implemented ports from actually present SATA devices;
- perform only the controller/port engine transitions strictly required to leave a coherent foundation state, with rollback/cleanup on failure.

DMA command lists, received-FIS buffers, command tables and PRDT storage remain M56 work unless M55 itself proves a concrete need for a minimal structure.

## Focused acceptance

The permanent M55 gate will boot the current q35 harness with a real AHCI controller and at least one attached SATA disk. Serial evidence must prove dynamic PCI BDF selection, the real ABAR, `CAP`, `VS`, `PI`, an implemented port, and a present SATA device whose `PxSSTS` and `PxSIG` came from controller state. The normal complete BoringOS boot must remain green.

## Strict non-goals

No sector read or write; no ATA data-path milestone; no BoringFS mount or persistent root through AHCI; no partition layer/MBR/GPT; no NCQ, port multiplier, hotplug, ATAPI, RAID, interrupt-driven or asynchronous AHCI I/O, NVMe, networking, SMP, or new generic block API.

## Semantic Freeze and closeout

Runtime Semantic Freeze: `9769aaa2acb44163749fc90ffb905038f891a427`, tree `c7d42d3e294ad577f33b7e3f79af4a4da361f587`. All 20 PR workflows on that exact runtime head were terminal SUCCESS before closeout, including focused M55 run `33247857135` and complete boot run `33247857153`.

The focused q35 run discovered the controller through the existing M44 PCI inventory at dynamic BDF `00:1f.2` (observed QEMU device `8086:2922`, evidence only), used real ABAR `0x00000000FEBD5000`, captured CAP `0xC0141F05`, CAP2 `0x00000000`, VS `0x00010000`, GHC `0x80000000`, PI `0x0000003F`, and found a real ATA SATA device on dynamically inspected port 0 with PxSSTS `0x00000113`, PxSIG `0x00000101`, DET=3 and IPM=1. M55 performed no sector read or write.

Active version after runtime-neutral closeout: **BoringKernel 0.0.56-dev**. No M56 implementation is included.
