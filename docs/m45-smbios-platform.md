# M45 — real bounded SMBIOS platform identity

Status: bounded scope; implementation and Semantic Freeze pending.

Verified base main `c78872a0877fbd7708c4b12a504de981899aae76`, tree
`abfd0beeb3fa4e0cc8c8b38ef4eb03999bc2df2e`, BoringKernel 0.0.45-dev; all
nine exact-main push workflows SUCCESS.

M45 adds a BoringOS-owned, allocation-free SMBIOS 2.x/3.x entry-point decoder
and bounded structure-table parser. It may expose real firmware vendor/version,
system manufacturer/product, board manufacturer/product and honestly available
bounded memory-device totals. Missing optional values remain unavailable.

Entry-point and table checks must cover anchors, checksums, advertised lengths,
physical-range arithmetic, mapped-memory containment, structure lengths,
double-NUL string-set termination, bounded string indices, table/structure
limits and memory-size overflow. Prefer a valid SMBIOS 3.x entry point and fall
back to a valid 2.x entry point. Never copy Linux DMI code, hardcode emulator
names, infer driver support or claim physical-hardware verification.

Host fixtures cover valid 2.x/3.x tables, absent optional fields, invalid
checksums, malformed/truncated structures, unterminated string sets, bad string
indices, table/structure bounds and memory-size overflow. Real QEMU acceptance
must read actual guest firmware tables in at least two meaningfully different
configurations when practical and reject hardcoded identity assumptions. Full
historical regression precedes Semantic Freeze and the runtime-neutral closeout
to BoringKernel 0.0.46-dev.

## Implementation boundary

The Limine SMBIOS response supplies candidate 3.x and 2.x entry-point pointers.
The kernel prefers a valid 3.x entry and falls back to a valid 2.x entry. It
validates anchors, both SMBIOS 2.x checksums, the SMBIOS 3.x checksum, entry
lengths, versions, physical table address, table size and the SMBIOS 2.x
structure count. The accepted table is limited to 1 MiB and 4,096 structures.
Its complete physical range must fit a non-bad Limine memory-map entry before
the checked HHDM translation is dereferenced.

The allocation-free parser requires every formatted structure to fit, every
string set to end in a bounded double NUL and every referenced string index to
exist. Strings are sanitized and copied into fixed 64-byte fields. Type 0
provides firmware vendor/version, type 1 system manufacturer/product and type 2
board manufacturer/product. Missing zero indices and absent structures stay
`unavailable`; malformed known structures reject the table.

Type 17 memory devices provide only honest bounded facts: slot count, installed
device count, summed KiB/MiB/extended-MiB sizes and an explicit completeness
flag when firmware reports an unknown size. Arithmetic is checked before every
range end, HHDM address and byte-total addition. No serial text is treated as an
API, and no userspace ABI changes in M45.

## Real acceptance

The focused QEMU acceptance builds the normal BoringOS image once, runs the
host fixtures and completes two independent normal boots:

- default q35, SMBIOS 3.0/64-bit entry: 9 structures, 382 table bytes,
  SeaBIOS `1.16.3-debian-1.16.3-2`, QEMU
  `Standard PC (Q35 + ICH9, 2009)`, board fields unavailable, one 128 MiB
  memory device;
- pc with forced SMBIOS 2.x/32-bit entry and deliberately supplied distinct
  values: 10 structures, 413 table bytes, `BoringFirmware45` / `BFW-45`,
  `BoringSystems45` / `Platform-45`, `BoringBoards45` / `Board-45`, one
  128 MiB memory device.

The supplied values are test inputs carried by the guest firmware table, not
production constants. Both scenarios reach the existing process/address-space
normal-boot witness. This proves real guest table consumption and both entry
formats while explicitly making no physical-PC or supported-hardware claim.
