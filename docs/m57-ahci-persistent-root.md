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

## Semantic Freeze and closeout

The M57 runtime is frozen at commit
`11f1810027e83c1268c4c01d51d13954f639ffba`, tree
`17ccb8e2c7da2e2c81d265d80b07ab001599c436`.

Focused workflow run `33252218157` passed the strict host fixtures and
ASAN/UBSAN, real q35 WRITE DMA and FLUSH CACHE completion, immediate readback,
neighbor-sector validation, a no-write persistence reboot, and the complete
two-boot USB-only desktop mutation scenario. The deterministic focused raw
image changed from SHA-256
`36e22f4b9332c930dc02e238719525dabb0f8bead4392d3aa68f03615077b581` to
`9e37362fabb28663bbb76110976cdc8fcd04dbb993bb6d77412ec1264516768c` and was
unchanged by the focused persistence reboot. The desktop image passed host
`boringfsck`, exact `/persist` byte validation and second-boot terminal-pixel
validation before its identical post-proof write. All 22 workflows passed on
the frozen head, including full boot run `33252218082` (#652).

Active version after runtime-neutral closeout: **BoringKernel 0.0.58-dev**. No
M58 implementation is included.
