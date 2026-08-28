# M43 — real CPUID hardware inventory

Status: bounded scope; implementation and Semantic Freeze pending.

Verified base main `0f6a35e0d182ef76dd7d5cda7a737f82661df178`, tree `df3fb4cb43c96ffd59a8d9641a98da4e0d918d89`, BoringKernel 0.0.43-dev; all seven exact-main push workflows SUCCESS.

Implement BoringOS-owned bounded x86_64 CPUID collection and decoding: vendor, optional brand, family/model/stepping, raw relevant feature registers and clearly scoped logical-processor information. Query supported leaves only. Emit the actual boot CPU inventory on the serial diagnostic path; no userspace ABI extension in M43 and no inference that advertised features are enabled or all CPUs are online.

Host fixtures cover supported/absent leaves, decoding and bounds. Real QEMU acceptance boots two CPU configurations and checks actual guest-derived values, including a configured vendor variation to reject hardcodes. Preserve all historical regression. Freeze before runtime-neutral closeout to 0.0.44-dev; exact-head CI, guarded squash and main-push SUCCESS required before M44.
