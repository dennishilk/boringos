# M50 — bounded xHCI EP0 descriptor discovery

Status: IMPLEMENTATION IN PROGRESS. This document anchors the M50 feature
branch before runtime changes; it is not acceptance evidence and does not claim
completion.

M50 is restricted to synchronous, serial standard `GET_DESCRIPTOR` control-IN
traffic on EP0 for directly attached root-port devices already addressed by
M49. The implementation must retrieve and validate the first eight bytes and
then the complete USB Device Descriptor, adjust EP0 maximum packet size through
a bounded Evaluate Context command only when the real descriptor requires it,
retrieve the Configuration Descriptor header and bounded full configuration
block, structurally validate it, and expose only generic descriptor facts.

The permanent acceptance target remains a real emulated `qemu-xhci` with the
QEMU `usb-kbd` and `usb-tablet`, using controller Transfer Events and descriptor
DMA contents rather than hardcoded device data.

M50 explicitly does not configure USB devices or endpoints, request HID/string/
BOS/report descriptors, perform interrupt transfers, deliver input, support
hubs/hotplug/storage, add a general control-transfer API, or begin M51.

Semantic Freeze SHA/tree and exact-head evidence will be recorded here only
after the M50 runtime implementation and focused real-QEMU acceptance are
complete.
