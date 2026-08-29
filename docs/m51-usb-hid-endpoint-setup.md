# M51 — bounded USB device configuration and HID endpoint setup

Status: implementation in progress.

M51 starts from the merged M50 addressed/descriptors_ready xHCI state and is limited to the smallest direct-root-port configuration path required for the QEMU usb-kbd and usb-tablet devices.

Planned runtime boundary:
- structurally walk the validated Configuration Descriptor
- identify bounded HID interfaces and Interrupt-IN endpoints from real descriptor data
- issue standard SET_CONFIGURATION on EP0
- allocate PMM-owned Interrupt-IN transfer rings
- construct 32-byte or 64-byte xHCI endpoint contexts from validated descriptor facts
- issue Configure Endpoint and validate the exact completion event
- publish device_configured / hid_endpoint_ready only after hardware success

Explicitly excluded: HID report transfers, keyboard/pointer event delivery, input-queue integration, hubs, hotplug, storage, generic control-transfer APIs, generic HID parsing and all M52 work.
