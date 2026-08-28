# M44 — real PCI inventory

Status: implementation candidate; exact-head CI and Semantic Freeze pending.

Verified base main `a13e9409d755efbbf7363b1857e9056bf04308ff`, tree `b425e2c2061861780a0296e68fb2e490f1a1ebb7`, BoringKernel 0.0.44-dev; all eight exact-main push workflows SUCCESS.

Implement a bounded BoringOS-owned general PCI inventory using real x86 PCI configuration access. Record bus/device/function, vendor/device ID, class/subclass/prog-if/revision and multifunction status. Numeric identities are authoritative; no device-name database or driver support fiction. Preserve the existing VirtIO block driver and correlate its actual selected PCI identity with the inventory. No userspace ABI change or PCIe extended-configuration support claim.

Host fixtures cover absent functions, multifunction and bounded storage/scan. Real QEMU proves actual emulated devices and VirtIO block correlation, with full historical regression. Freeze precedes runtime-neutral closeout to 0.0.45-dev; exact-head CI, guarded squash and main-push SUCCESS required before M45.


## Implementation boundary

The existing x86 CF8/CFC configuration reader is reused without modification.
A separate allocation-free collector scans segment zero, 256 bus numbers and
32 slots. Function zero must exist; only its multifunction bit permits probing
functions 1–7. It reads identity, class/revision and header dwords only. No BAR
sizing, bus renumbering, configuration-space writes, bus-master enable or driver
binding is performed by the inventory. The configuration address port is only
used to select reads. Collection occurs during the single-CPU early boot path,
before interrupts or competing PCI clients. It is not a concurrent hotplug API.

At most 65,536 functions / 196,608 config reads; at most 256 stored records.
The total count continues past storage capacity and `truncated` records that
loss. Read errors return an incomplete partial snapshot, never complete success.
Records retain BDF, numeric vendor/device IDs, class/subclass/prog-if, revision
and raw header/multifunction bits. The fixed boot snapshot has a kernel getter;
no userspace ABI or wire layout changes.

This discovers firmware-numbered PCI/PCIe functions reachable through the first
256 configuration bytes in segment zero. It does not claim ECAM, other PCI
segments, PCIe extended capabilities, bridge configuration or support for the
devices whose identities it reports.

## Real acceptance

The new workflow runs the unchanged complete `virtio-block-qemu.sh` acceptance,
including actual guest reads/writes, queue wrap, invalid-range rejection and
independent host checks of persisted sectors and untouched neighbors. Its dynamic
selected-driver BDF must identify exactly one inventory record with the same
1AF4:1042 identity and mass-storage class. Actual chipset secondary functions
must have an inventoried function zero with the multifunction bit set.

Local QEMU inventory: six devices, 8,211 config reads, no truncation, complete.
The actual block driver selected 00:02.0, matching inventory 1AF4:1042, class
01:00. Other actual records include 8086:29C0, 1234:1111, and 8086:2918/2922/2930.
These are emulated-device evidence, not host or physical-PC identities. CI may
include a different set/BDF because its emulator configuration differs; the
acceptance correlates dynamic identities instead of hardcoding this address.

Host tests cover missing devices, single/multifunction rules, sparse functions,
last bus/slot/function, all 65,536 possible functions, the 256-entry storage cap,
partial read errors even after truncation, empty scans and canaries. ASan/UBSan
passes locally with leak detection disabled for the worker inspection limit.
The PCI access implementation and VirtIO driver/test themselves are unchanged.

No new OS executable, userspace ABI, device-name database or hardware support
claim. Version remains 0.0.44-dev until exact-head Semantic Freeze.
