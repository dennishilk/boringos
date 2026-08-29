# M57 — writable AHCI and persistent BoringFS root

## Scope

M57 extends the completed M56 synchronous AHCI backend with the smallest bounded writable SATA DMA path and uses that same backend beneath the existing M21 generic block-device, BoringFS and VFS stacks.

The focused integration target is q35 with i8042 disabled, qemu-xhci keyboard/tablet, a real AHCI/SATA controller and a separate RAW BoringFS root image. The root must not be supplied by VirtIO.

Required runtime proof:

- IDENTIFY-derived write/cache capabilities
- bounded WRITE DMA EXT, with a capability-correct fallback only when required
- the existing single-request command structures, one PRDT entry and 4-KiB bounce strategy
- checked LBA, byte, sector-count and PRDT arithmetic before hardware submission
- bounded PxTFD/PxCI/PxIS/TFES and timeout/error handling
- the smallest required ATA cache-flush path before claiming persistence
- BoringFS root mounted through the existing M21 device and existing filesystem/VFS paths
- boring-init, native desktop and USB-only Super+Return terminal startup
- a real existing-userland file mutation, host-side BoringFS validation and exact reboot persistence
- intact neighboring data and complete session/resource drain

The acceptance image is booted twice without regeneration. The first USB-only
desktop session creates `/persist` and writes `survived\n` through the existing
shell, VFS, BoringFS, M21 block-device and AHCI layers. With QEMU stopped, the
host validates the whole image and exact file bytes. The second session reads
the file before any write, validates those bytes from the rendered terminal,
then performs an identical rewrite only to exercise the closeout write/flush
accounting; the final whole-image hash must remain identical.

## Strict non-goals

M57 does not add MBR/GPT, partitions, NCQ, hotplug, port multipliers, ATAPI, RAID, asynchronous or interrupt-driven AHCI redesign, NVMe, networking, audio, SMP, physical-hardware success claims or M58 work.
