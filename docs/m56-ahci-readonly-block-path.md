# M56 — Real Synchronous Read-Only AHCI SATA Block Path

Status: scope anchor; runtime implementation follows on this branch.

M56 extends the proven M55 AHCI/SATA controller foundation with the smallest bounded real data path needed to expose one discovered SATA disk as a read-only backend through the existing M21 generic block-device architecture.

## Architecture boundary

`PCI/AHCI hardware → synchronous SATA read backend → existing M21 generic block-device layer → existing BoringFS/VFS/userspace`

M56 does not create a second block API, second PCI path, new filesystem path, or asynchronous storage subsystem.

## Runtime scope

M56 is limited to:

- PMM-owned, alignment-correct AHCI command list / received-FIS / command-table storage required by the read path
- bounded PRDT construction and checked physical-address/range arithmetic
- one synchronous command at a time
- `IDENTIFY DEVICE` with defensive capability, logical-sector-size and capacity parsing
- a bounded DMA read command path suitable for the discovered SATA device (normally `READ DMA EXT` when supported)
- strict LBA, byte-count, command-size and PRDT bounds before touching hardware
- bounded CI/PxTFD/PxIS completion and error handling with timeout
- cleanup after partial initialization or failed commands
- registration as a read-only backend through the existing generic block-device API
- focused q35 + AHCI + deterministic raw SATA image acceptance proving first/middle/last-LBA and multi-sector reads from real emulated hardware

## Explicit non-goals

M56 does not add:

- AHCI writes
- writable or persistent BoringFS root over AHCI
- partition parsing (MBR/GPT)
- NCQ
- hotplug
- ATAPI
- RAID
- asynchronous or interrupt-driven AHCI I/O
- NVMe
- M57 work

Observed QEMU vendor/device IDs, BDFs, ports, capacities and geometry are evidence only and must not become production constants.
