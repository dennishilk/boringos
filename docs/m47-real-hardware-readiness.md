# M47 — real-hardware boot-readiness boundary

Status: implementation complete on BoringKernel 0.0.47-dev; exact-head CI and
Semantic Freeze pending.

Verified base main `fc4f6d0dd26ff985a4336362b4a32bd0f9ea7f91`, tree
`229653f11d94c3a15b25ca9bc417773dfa1a1a97`, with all eleven exact-main push
workflows successful.

## Result

The current platform is a **REAL-HARDWARE-READY CANDIDATE** for a deliberately
legacy-assisted x86_64 UEFI configuration. It is **NOT PHYSICALLY VERIFIED**
and is not ready for a typical modern PC. OVMF proves the UEFI handoff and
isolates the current boundaries; it is emulator evidence, not physical-machine
evidence.

The first interactive blocker is xHCI/USB HID input. The independent persistent
root blocker is AHCI/NVMe storage. Numeric PCI discovery of those controllers
does not constitute driver support. M48 is therefore the smallest correct
xHCI/USB-HID foundation; AHCI/NVMe remains explicit later work.

## Readiness matrix

| Area | Implemented boundary | OVMF evidence | Remaining assumption or blocker | Verdict |
|---|---|---|---|---|
| Boot | Pinned Limine hybrid image and x86_64 UEFI handoff | EDK II boots the normal and desktop images | No Secure Boot path and no physical firmware test | Candidate only |
| Memory | Validated Limine map; 4-KiB PMM; at most 1,048,576 managed frames and 64 usable regions | A 5-GiB VM reaches the normal completion marker while reporting exactly 4 GiB managed and `Memory map capped: yes` | Excess usable memory is deliberately left unmanaged; at most 256 map entries are accepted | Candidate with explicit cap |
| Framebuffer | Validated Limine framebuffer, RGB 24/32-bpp rendering | GOP-provided 1280x800 and 800x600 framebuffers are detected | Firmware must supply a supported linear framebuffer; no mode setting or fallback | Candidate under contract |
| CPU inventory | Bounded CPUID inventory of the boot CPU | Real `qemu64` CPUID path completes | Single boot CPU only; no SMP bring-up or topology control | Inventory candidate only |
| PCI inventory | Read-only segment-zero CF8/CFC scan, first 256 config bytes | q35 devices, xHCI `1B36:000D` and AHCI `8086:2922` are observed numerically | Firmware-configured buses only; no ACPI MCFG/ECAM, bridge setup or binding inference | Partial inventory only |
| SMBIOS | Optional bounded Limine SMBIOS 2.x/3.x validation and parsing | EDK II platform and 5-GiB memory facts are consumed | Optional identity source, not a boot dependency | Candidate under contract |
| Input | i8042 PS/2 keyboard and mouse only | Legacy-assisted desktop reports the real PS/2 path; `q35,i8042=off` plus xHCI stops exactly at `input hardware` | No xHCI or USB HID driver | **Blocker** |
| Storage | Modern VirtIO 1.x PCI block and BoringFS root only | VirtIO root mounts and desktop runs; AHCI-only root stops exactly at `persistent root` | No AHCI or NVMe driver and no general partition layer | **Independent blocker** |
| Desktop | Native display/WM/apps on the existing input and root contracts | OVMF + i8042 + VirtIO reaches desktop state `RUNNING` | Typical USB-input and AHCI/NVMe-root machines do not satisfy those contracts | Legacy-assisted candidate only |

## Platform assumptions made explicit

- ACPI is not consumed: there is no Limine RSDP request, MADT parsing or MCFG
  parsing.
- Interrupt and timer operation requires the legacy 8259 PIC and PIT channel 0;
  APIC routing is not enabled.
- PCI access is legacy CF8/CFC on segment zero. The kernel neither configures
  bridges nor uses ECAM.
- PS/2 success requires a working firmware/platform i8042 path. Enumerating an
  xHCI controller does not provide USB input.
- Persistent desktop startup requires a modern VirtIO block device containing
  the expected BoringFS root image.
- Limine must provide the validated memory map, HHDM, framebuffer and boot
  modules required by the selected boot mode. SMBIOS may be absent.

## Reproducible evidence

`tests/m47-ovmf-readiness.py` runs with QEMU 8.2.2 and OVMF 2024.02 in the
reference environment. It records serial and QEMU logs, scenario metadata,
firmware/image hashes and a SHA-256 manifest for four controlled boots:

1. normal UEFI boot with 5 GiB RAM and the explicit 4-GiB PMM cap;
2. legacy-assisted i8042 + VirtIO desktop success;
3. xHCI USB keyboard/mouse with i8042 disabled, expected input failure;
4. AHCI-only root, expected persistent-root failure.

The focused PMM host regression also proves that a larger valid map is safely
capped, a small map remains uncapped, and overlapping or overflowing maps still
fail closed. The desktop acceptance now reports subsystem, input-hardware and
persistent-root failures separately; this changes diagnostics only.

No USB, xHCI, AHCI, NVMe or ACPI implementation is hidden in M47, and no result
in this milestone is a physical-machine verification claim.
