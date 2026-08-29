# M49 — bounded xHCI USB device addressing

Status: scope fixed; implementation in progress on BoringKernel 0.0.49-dev.

M49 extends the existing M48 command/event transport only far enough to reset
connected root ports, issue and validate Enable Slot and Address Device
commands, construct bounded PMM-owned slot/EP0 contexts, install DCBAAP slot
entries and retain an explicit root-port-to-slot addressed-device state.

The implementation is limited to directly attached root-port devices. It does
not request descriptors, expose arbitrary control transfers, configure HID
interrupt endpoints, consume device-traffic transfer events, connect the M48
HID decoder to hardware, deliver input, support hubs or add storage drivers.
QEMU evidence is emulator evidence and is not a physical-hardware claim.
