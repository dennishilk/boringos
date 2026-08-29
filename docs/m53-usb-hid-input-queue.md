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

The focused acceptance consumes real HID completions one at a time under a fixed upper bound and succeeds only after the canonical queue reaches the required final state. Neutral HID reports may therefore add transport completions, but they do not relax the required seven-event Super+A / tablet-movement / left-button ordering or the final modifier-release state.

M53 must not create a second input stack and must not expand into the full i8042-free desktop closeout reserved for a later milestone. USB storage, AHCI/NVMe, installer, networking, audio and M54 semantics are out of scope.


## Semantic Freeze and closeout

Runtime Semantic Freeze: `8063bea362f5191a23f3a6f6465d7ec55ad3e084`, tree `50031e31bd200e93fb36b75e3abe50fdf36b6ff0`. All 18 PR workflows on that exact runtime head were terminal SUCCESS before closeout, including M53 focused run `33234820597` and full Boot #601.

The focused real-QEMU witness used `q35,i8042=off`, `qemu-xhci`, `usb-kbd` and `usb-tablet`. It completed 10 real USB HID transfers, decoded 10 reports, produced exactly seven events in the existing canonical input queue, dropped zero events and finished with no modifiers held. The exact witnessed queue ordering was Left Super down, A down, tablet movement 2345 x 3456, left button down, A up, left button up and Left Super up.

Active version after runtime-neutral closeout: **BoringKernel 0.0.54-dev**. No M54 implementation is included and no full PS/2-free desktop claim is made by M53.
