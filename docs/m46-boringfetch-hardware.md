# M46 — boringfetch hardware edition

Status: implementation complete on BoringKernel 0.0.46-dev; Semantic Freeze
pending exact-head CI.

Verified base main `4411bef3930450b9b5593641be3e40cdfe4c8f8e`, tree
`1d2e4f2b35dbc595341214aeaf406c579da15b48`, with all ten exact-main push
workflows successful.

M46 makes the existing native Ring 3 `boringfetch` client a bounded view of
hardware facts already owned by BoringOS. The system-information syscall may
grow through an explicitly versioned in-tree ABI, but it must copy a fixed-size
snapshot only: no userspace pointers, unbounded arrays, kernel addresses or
serial-text scraping.

When the corresponding source is available, the client displays:

- BoringOS, kernel name/version and architecture;
- real CPUID vendor, brand, family, model and stepping;
- real SMBIOS system manufacturer/product, board manufacturer/product and
  firmware vendor/version;
- the real PCI device count plus a bounded sample of numeric BDF,
  Vendor:Device and class codes;
- kernel usable/free memory and separately labelled SMBIOS memory facts;
- the real active framebuffer geometry;
- only detected block-device facts, including bounded name, capacity and the
  numeric PCI identity of an initialized VirtIO block device where applicable.

Unavailable facts are omitted or explicitly unavailable; they are never
invented. Numeric PCI identity is descriptive inventory, not a driver-support
claim, and an absent driver never becomes reported support. Existing
filesystem, process and uptime facts remain kernel-derived.

Acceptance launches the normal graphical desktop, opens a terminal through
`Super+Return`, runs the real `/bin/boringfetch` Ring 3 ELF and proves visible
hardware values in the framebuffer. The focused gate also proves BoringEdit,
BoringFiles and their documented shortcuts still work. Host tests cover ABI
bounds, unavailable fields, exact numeric formatting and malformed snapshots.
All historical workflows must pass at the exact implementation head before
Semantic Freeze. A separate runtime-neutral closeout then advances the active
version witnesses to BoringKernel 0.0.47-dev, followed by exact-head closeout
CI and a guarded squash merge.

## Implementation boundary

`INFO` ABI v3 is a zero-initialized, fixed 1,024-byte snapshot. Availability
bits independently identify CPUID, system, board, firmware, SMBIOS memory, PCI,
framebuffer, storage and storage-PCI facts; completeness bits distinguish a
complete PCI walk and complete SMBIOS memory sizing. It carries at most eight
12-byte PCI samples and the independent total count. All strings remain fixed
and NUL-terminated, all arithmetic is checked and reserved bytes must remain
zero. Boringfetch rejects the wrong ABI version, an excessive sample count or
any unterminated field before rendering.

The kernel populates the snapshot directly from `boring_cpu_inventory_get`,
`boring_platform_identity_get`, `boring_pci_inventory_get`,
`boring_framebuffer_get` and the initialized VirtIO block device/stats. The
client prints uppercase numeric PCI identity and class codes, and sizes small
detected disks in KiB rather than rounding them to a misleading zero MiB.
Unavailable optional fields are omitted. This remains a normal standalone
Ring 3 ELF using the existing syscall and file-descriptor boundary.

## Real acceptance

The focused QEMU run opened the terminal with `Super+Return` and decoded the
entire 800x600 framebuffer pixel-for-pixel. It cross-checked visible text
against independently emitted boot inventory: AuthenticAMD / QEMU Virtual CPU
family 15 model 107 stepping 1; QEMU q35 system and SeaBIOS identity; seven
complete numeric PCI entries; one complete 128 MiB SMBIOS memory device; an
800x600x32 framebuffer with pitch 3,200; and initialized `vblk0`, 144 KiB,
`1AF4:1042`. Board identity was unavailable and therefore absent from the
client. These are actual focused-test guest facts, not production constants.

The same gate reran the real three-client scenario: `Super+Return`, `Super+E`
and `Super+F` launched independent terminal, BoringEdit and BoringFiles
processes; focused input stayed isolated, editor bytes persisted, `Super+Q`
closed each client gracefully and all resources drained back to PID 1. Host
tests separately cover exact hardware formatting, unavailable-field omission,
sample bounds and unterminated snapshots.
