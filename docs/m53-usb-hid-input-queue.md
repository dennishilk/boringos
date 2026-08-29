# M53 — USB HID integration with the existing input queue

Milestone 53 is intentionally limited to connecting the real xHCI HID report path proven by M52 to the existing BoringOS input infrastructure.

Required architecture:

```text
xHCI
→ M52 real Interrupt-IN completion
→ bounded HID decoder
→ existing kernel/input queue
→ existing INPUT_READ / event semantics
→ existing consumers
```

Acceptance must prove both keyboard and pointer events originate from real QEMU USB devices through qemu-xhci with the legacy i8042 path disabled. Direct synthetic injection at the queue/output end is not acceptable evidence.

M53 must not create a second input stack and must not expand into the full i8042-free desktop closeout reserved for a later milestone. USB storage, AHCI/NVMe, installer, networking, audio and M54 semantics are out of scope.
