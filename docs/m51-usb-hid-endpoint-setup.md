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


Implementation notes:
- only alternate setting zero HID interfaces are selected; M51 does not issue SET_INTERFACE
- HID Interrupt-IN endpoint address, packet size and interval are taken from validated configuration bytes
- endpoint DCI is derived from the USB endpoint address
- Full/Low-Speed interrupt intervals use the xHCI base-2 125 us encoding; High-Speed uses bInterval-1
- SuperSpeed HID endpoint configuration is rejected in M51 because companion-descriptor semantics are deliberately outside this bounded milestone
- the EP0 extension is exactly standard SET_CONFIGURATION with no data stage
- endpoint transfer rings are PMM-owned and no HID report TRB is submitted in M51
