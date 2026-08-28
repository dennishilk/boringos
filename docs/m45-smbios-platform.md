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
