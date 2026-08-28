# M44 — real PCI inventory

Status: bounded scope; implementation and Semantic Freeze pending.

Verified base main `a13e9409d755efbbf7363b1857e9056bf04308ff`, tree `b425e2c2061861780a0296e68fb2e490f1a1ebb7`, BoringKernel 0.0.44-dev; all eight exact-main push workflows SUCCESS.

Implement a bounded BoringOS-owned general PCI inventory using real x86 PCI configuration access. Record bus/device/function, vendor/device ID, class/subclass/prog-if/revision and multifunction status. Numeric identities are authoritative; no device-name database or driver support fiction. Preserve the existing VirtIO block driver and correlate its actual selected PCI identity with the inventory. No userspace ABI change or PCIe extended-configuration support claim.

Host fixtures cover absent functions, multifunction and bounded storage/scan. Real QEMU proves actual emulated devices and VirtIO block correlation, with full historical regression. Freeze precedes runtime-neutral closeout to 0.0.45-dev; exact-head CI, guarded squash and main-push SUCCESS required before M45.
