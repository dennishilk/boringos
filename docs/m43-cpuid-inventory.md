# M43 — real CPUID hardware inventory

Status: implementation COMPLETE and Semantic Frozen; runtime-neutral closeout to 0.0.44-dev.

Verified base main `0f6a35e0d182ef76dd7d5cda7a737f82661df178`, tree `df3fb4cb43c96ffd59a8d9641a98da4e0d918d89`, BoringKernel 0.0.43-dev; all seven exact-main push workflows SUCCESS.

Implement BoringOS-owned bounded x86_64 CPUID collection and decoding: vendor, optional brand, family/model/stepping, raw relevant feature registers and clearly scoped logical-processor information. Query supported leaves only. Emit the actual boot CPU inventory on the serial diagnostic path; no userspace ABI extension in M43 and no inference that advertised features are enabled or all CPUs are online.

Host fixtures cover supported/absent leaves, decoding and bounds. Real QEMU acceptance boots two CPU configurations and checks actual guest-derived values, including a configured vendor variation to reject hardcodes. Preserve all historical regression. Freeze before runtime-neutral closeout to 0.0.44-dev; exact-head CI, guarded squash and main-push SUCCESS required before M44.

## Inventory contract

The boot processor executes the real x86_64 CPUID instruction. Collection is
allocation-free and bounded to eight queries: base/extended maxima, optional
leaf 1, optional leaf 7 subleaf 0, optional extended leaf 1, and three brand
leaves only when all are advertised. Unsupported fields remain zero with
explicit validity flags. Vendor is 12 bytes plus terminator; brand is 48 plus
terminator. Nonprinting string bytes are sanitized for serial diagnostics.

Family/model obey base-family-specific extended decoding. The inventory retains
raw signature, initial APIC ID and relevant feature registers without guessing
names or enabled OS support. The existing syscall ABI is unchanged. Kernel
consumers can read the bounded immutable boot snapshot through the getter;
serial diagnostics expose its actual values now. Userspace presentation is
reserved for the later hardware edition milestone.

| Register | Examples of advertised feature bits retained |
| --- | --- |
| leaf 1 EDX | FPU 0, TSC 4, MSR 5, PAE 6, APIC 9, SSE 25, SSE2 26, HTT 28 |
| leaf 1 ECX | SSE3 0, SSSE3 9, SSE4.1 19, SSE4.2 20, XSAVE 26, OSXSAVE 27, AVX 28, hypervisor 31 |
| leaf 7 EBX | FSGSBASE 0, BMI1 3, AVX2 5, SMEP 7, BMI2 8, SMAP 20 |
| extended leaf 1 EDX | SYSCALL 11, NX 20, 1GiB pages 26, RDTSCP 27, long mode 29 |

All listed registers plus leaf 7 ECX/EDX and extended leaf 1 ECX are preserved.
No feature is enabled by this inventory code. CPUID leaf 1's logical count is
only the maximum addressable logical IDs in the package when HTT is advertised;
otherwise one. Zero from an advertised but empty HTT count stays zero/unknown.
It is not a physical-core count, detected socket count or online CPU count.

## Acceptance and observed platform boundary

Host fixtures cover eight-query bounds, missing leaves, partial brand leaf
availability, Intel/AMD-style family/model rules, untouched canaries and exact
raw bits. ASan/UBSan is also run locally with LeakSanitizer disabled for the
worker's process-inspection restriction.

The actual QEMU test requires complete normal boot for two single-CPU variants.
The second config deliberately changes the emulated vendor to `BoringCPU123`,
family to 15, model to 42 and stepping to 7; those are test inputs, never kernel
constants. JSON and serial evidence contain values read by the guest.
A third four-CPU scenario proves only early CPUID inventory and a reported
package maximum of four; it explicitly does not claim an SMP runtime boot.

During initial testing, a four-CPU normal boot reached `Interrupts: enabled`
but did not complete the PIT-driven normal acceptance. The pre-M43 M42 base independently reproduces the same four-CPU stall,
so CPUID collection did not introduce it. The documented supported
reference remains one `qemu64,apic=off` CPU with legacy PIC/PIT. No SMP/APIC fix
is included or claimed. The single-CPU full permanent regression is unchanged.

No physical-machine evidence, new executable or userspace ABI extension.
Implementation stayed at 0.0.43-dev; the proven freeze permits the separate
runtime-neutral closeout to 0.0.44-dev.


Local real guest results: standard qemu64 reports vendor AuthenticAMD, brand
QEMU Virtual CPU version 2.5+, family/model/stepping 15/107/1; the changed
single-CPU variant reports BoringCPU123 and 15/42/7. Both complete normal boot.
The additional four-CPU inventory reports logical_per_package_max=4.
These values describe these emulated CPUs, not the host or a physical PC.
All exact-head implementation gates completed SUCCESS; the freeze is recorded below.


## Semantic Freeze

Implementation: `2973b2bba0a86da0332008c1feacb0201baca791`.
Tree: `bd0b4cf05027c020be55459206bcf0e90cd1f30c`.
Exact-head SUCCESS: M43 #1 / 33194111893; M42 #5 / 33194111742;
M41 #9 / 33194111733; M40 #12 / 33194111694; M39 #15 / 33194111690;
M38 #26 / 33194111726; M37 #59 / 33194111685; complete Boot #525 / 33194111719.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.
