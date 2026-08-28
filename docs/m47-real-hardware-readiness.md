# M47 — real-hardware boot-readiness boundary

Status: scoped on BoringKernel 0.0.47-dev; audit and focused proof pending.

Verified base main `fc4f6d0dd26ff985a4336362b4a32bd0f9ea7f91`, tree
`229653f11d94c3a15b25ca9bc417773dfa1a1a97`, with all eleven exact-main push
workflows successful.

M47 audits the current BoringOS platform contract for real x86_64 UEFI
machines. It must produce an evidence-backed matrix for boot, memory,
framebuffer, CPU inventory, PCI inventory, SMBIOS, input, storage and desktop.
Each row distinguishes implemented code, emulator evidence, unresolved
firmware/hardware assumptions and the first concrete blocker.

The audit explicitly covers the Limine UEFI path, GOP framebuffer, Limine
memory-map assumptions, CPUID, SMBIOS, ACPI availability, PCI configuration
access, PIC/PIT routing, PS/2 versus xHCI input, VirtIO-only storage and the
persistent BoringFS root dependency. Where the environment permits, a separate
OVMF/UEFI QEMU boot must exercise the normal image and collect machine-readable
evidence without being presented as a physical-machine test.

M47 may include only small platform-neutral or runtime-neutral corrections
found by the audit. It must not hide xHCI, USB HID, NVMe or AHCI implementation,
claim that numeric inventory means driver support, or say `PHYSICALLY VERIFIED`
without external physical evidence. Its output must identify the exact first
real-hardware boundary and thereby select M48 honestly.

All inherited workflows must pass on the exact implementation head before
Semantic Freeze. A separate runtime-neutral closeout advances active version
witnesses to BoringKernel 0.0.48-dev, followed by exact-head closeout CI and a
guarded squash merge.
