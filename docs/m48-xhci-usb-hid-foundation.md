# M48 — xHCI / USB-HID foundation

Status: implementation candidate on BoringKernel 0.0.48-dev.

## Proven boundary

M47 identified USB input as the first interactive modern-PC blocker. M48 starts
that path without converting PCI inventory into a support claim.

The BoringOS-owned xHCI foundation:

- binds only a real segment-zero PCI class `0C:03:30` function;
- enables PCI memory decoding and bus mastering, then maps BAR0 through the
  bounded cache-disabled MMIO window;
- validates capability length, slots, interrupters, ports, runtime, doorbell
  and extended-capability offsets before using them;
- walks at most 64 extended capabilities and performs the bounded BIOS/OS
  ownership handoff when the legacy-support capability exists;
- stops and resets the controller with bounded waits;
- rejects scratchpad-requiring controllers for now instead of programming an
  incomplete scratchpad array;
- allocates and zeroes PMM-owned DCBAA, command-ring, event-ring and ERST
  frames, installs a cycle-correct Link TRB, and starts the controller;
- observes at most 64 real port connect-state bits; and
- performs no USB device addressing, descriptor requests or endpoint setup.

The separate pure USB-HID boot-protocol decoder accepts only exact eight-byte
keyboard reports and three/four-byte mouse reports. It rejects rollover/error
usages, duplicate key usages and output overflow, and reports bounded key
transitions, modifiers, signed movement, wheel and five button bits.

## Evidence

Host fixtures plus ASan/UBSan cover valid and malformed xHCI capabilities,
offset/port bounds, key press/add/release state, invalid report preservation,
rollover rejection and signed mouse reports.

The focused QEMU q35 acceptance adds a real emulated `1B36:000D` xHCI
controller, USB keyboard and USB tablet. The guest itself proves eight ports,
64 slots, two connected ports, successful controller start and installed
command/event DMA transport.

## Explicit remaining blocker

This milestone candidate is a controller and decoder foundation, not complete
USB input. It does **not** yet issue Enable Slot / Address Device, consume USB
descriptors, configure HID interrupt endpoints, poll transfer events or submit
decoded reports to the BoringOS input queue. Therefore the desktop is not yet
claimed operable with i8042 disabled, and there is no physical-hardware claim.

AHCI/NVMe persistent-root support remains the independent M47 storage blocker.
