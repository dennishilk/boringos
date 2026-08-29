#ifndef BORING_XHCI_H
#define BORING_XHCI_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/pci.h>
#include <boring/usb_hid.h>

#define XHCI_MMIO_WINDOW_SIZE 65536U
#define XHCI_MAX_PORTS 64U
#define XHCI_MAX_ADDRESSED_DEVICES 8U
#define XHCI_TRB_SIZE 16U
#define XHCI_COMMAND_RING_USABLE 252U
#define XHCI_EP0_RING_USABLE 252U
#define XHCI_EVENT_RING_TRBS 256U
#define XHCI_DESCRIPTOR_BUFFER_BYTES 4096U
#define XHCI_INTERRUPT_RING_USABLE 252U
#define XHCI_MAX_HID_ENDPOINTS 4U
#define XHCI_HID_REPORT_BUFFER_BYTES 4096U

#define XHCI_TRB_TYPE_NORMAL 1U
#define XHCI_TRB_TYPE_SETUP_STAGE 2U
#define XHCI_TRB_TYPE_DATA_STAGE 3U
#define XHCI_TRB_TYPE_STATUS_STAGE 4U
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12U
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13U
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32U
#define XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT 33U
#define XHCI_COMPLETION_SUCCESS 1U
#define XHCI_COMPLETION_SHORT_PACKET 13U
#define XHCI_USB_DESCRIPTOR_DEVICE 1U
#define XHCI_USB_DESCRIPTOR_CONFIGURATION 2U
#define XHCI_USB_DESCRIPTOR_INTERFACE 4U
#define XHCI_USB_DESCRIPTOR_ENDPOINT 5U
#define XHCI_USB_CLASS_HID 3U
#define XHCI_USB_ENDPOINT_TRANSFER_INTERRUPT 3U

struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct xhci_usb_descriptor_facts {
    uint16_t usb_version;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t configuration_length;
    uint16_t ep0_max_packet;
    uint8_t device_class;
    uint8_t configuration_count;
    uint8_t interface_count;
    uint8_t b_max_packet_size0;
};

struct xhci_hid_endpoint_descriptor {
    uint16_t max_packet;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t protocol;
    uint8_t endpoint_address;
    uint8_t endpoint_id;
    uint8_t interval;
    uint8_t xhci_interval;
};

struct xhci_hid_configuration {
    struct xhci_hid_endpoint_descriptor endpoints[XHCI_MAX_HID_ENDPOINTS];
    uint8_t configuration_value;
    uint8_t endpoint_count;
};

struct xhci_hid_endpoint_runtime {
    uint64_t report_buffer_physical;
    uint64_t expected_trb_physical;
    struct usb_hid_keyboard_state keyboard_state;
    uint32_t submitted_transfers;
    uint32_t completed_transfers;
    uint32_t short_packets;
    uint32_t report_bytes;
    uint32_t decoded_reports;
    uint32_t key_presses;
    uint32_t key_releases;
    uint32_t pointer_reports;
    uint16_t producer_index;
    uint16_t last_report_length;
    uint16_t last_pointer_x;
    uint16_t last_pointer_y;
    uint8_t last_key_usage;
    uint8_t last_pointer_buttons;
    bool producer_cycle;
    bool transfer_outstanding;
    bool last_key_down;
    bool pointer_valid;
};

struct xhci_control_td {
    struct xhci_trb setup;
    struct xhci_trb data;
    struct xhci_trb status;
    uint64_t setup_physical;
    uint64_t data_physical;
    uint64_t status_physical;
    uint16_t next_producer_index;
    bool next_producer_cycle;
};

struct xhci_addressed_device {
    uint64_t input_context_physical;
    uint64_t device_context_physical;
    uint64_t ep0_ring_physical;
    uint64_t descriptor_buffer_physical;
    uint64_t hid_ring_physical[XHCI_MAX_HID_ENDPOINTS];
    uint64_t expected_data_trb_physical;
    uint64_t expected_status_trb_physical;
    struct xhci_usb_descriptor_facts descriptors;
    struct xhci_hid_configuration hid_configuration;
    struct xhci_hid_endpoint_runtime hid_runtime[XHCI_MAX_HID_ENDPOINTS];
    uint32_t transfer_events;
    uint32_t descriptor_bytes;
    uint32_t short_packets;
    uint32_t evaluate_context_completions;
    uint32_t set_configuration_completions;
    uint32_t configure_endpoint_completions;
    uint16_t ep0_producer_index;
    uint16_t ep0_max_packet;
    uint16_t outstanding_length;
    uint8_t root_port_id;
    uint8_t slot_id;
    uint8_t speed;
    bool ep0_producer_cycle;
    bool control_outstanding;
    bool descriptors_ready;
    bool device_configured;
    bool hid_endpoint_ready;
    bool addressed;
};

struct xhci_capabilities {
    uint8_t capability_length;
    uint8_t max_slots;
    uint8_t max_ports;
    uint16_t max_interrupters;
    uint16_t scratchpad_count;
    uint32_t doorbell_offset;
    uint32_t runtime_offset;
    uint16_t extended_capability_offset;
    bool context_64_bytes;
};

struct xhci_state {
    struct pci_device device;
    struct xhci_capabilities capabilities;
    uint64_t mmio_physical;
    uint64_t dcbaa_physical;
    uint64_t command_ring_physical;
    uint64_t event_ring_physical;
    uint64_t erst_physical;
    uint64_t connected_ports;
    struct xhci_addressed_device addressed[XHCI_MAX_ADDRESSED_DEVICES];
    uint32_t command_completions;
    uint32_t port_events_consumed;
    uint8_t addressed_count;
    bool addressing_truncated;
    bool legacy_handoff_complete;
    bool controller_running;
};

/* Pure bounded capability parser used by the hardware path and host fixtures. */
bool xhci_parse_capabilities(const volatile void *mmio, uint32_t length,
                             struct xhci_capabilities *capabilities);

/* Pure M49 model helpers shared by host fixtures and the hardware path. */
bool xhci_ep0_max_packet(uint8_t speed, uint16_t *max_packet);
bool xhci_build_address_input_context(void *buffer, uint32_t length,
                                      bool context_64_bytes,
                                      uint8_t max_slots, uint8_t slot_id,
                                      uint8_t max_ports, uint8_t root_port_id,
                                      uint8_t speed,
                                      uint64_t ep0_ring_physical);
bool xhci_validate_command_completion(const struct xhci_trb *event,
                                      uint64_t expected_command_physical,
                                      uint8_t max_slots,
                                      uint8_t *slot_id);

/* Pure M50 model helpers. They do not issue hardware traffic. */
bool xhci_build_get_descriptor_control_td(struct xhci_control_td *td,
                                          uint64_t ep0_ring_physical,
                                          uint16_t producer_index,
                                          bool producer_cycle,
                                          uint64_t buffer_physical,
                                          uint8_t descriptor_type,
                                          uint8_t descriptor_index,
                                          uint16_t length);
bool xhci_validate_control_transfer_event(
    const struct xhci_trb *event, uint64_t ep0_ring_physical,
    uint8_t expected_slot_id, uint64_t expected_data_trb_physical,
    uint64_t expected_status_trb_physical, uint16_t requested_length,
    bool expect_status_only, uint16_t *actual_length, bool *short_packet);
bool xhci_descriptor_ep0_max_packet(uint8_t speed, uint8_t descriptor_value,
                                    uint16_t *max_packet);
bool xhci_build_evaluate_ep0_context(void *buffer, uint32_t length,
                                     bool context_64_bytes,
                                     uint8_t max_slots, uint8_t slot_id,
                                     uint16_t max_packet,
                                     uint64_t ep0_ring_physical);
bool xhci_validate_device_descriptor_prefix(const uint8_t *bytes,
                                            uint16_t received,
                                            uint8_t speed,
                                            uint16_t *max_packet);
bool xhci_validate_device_descriptor(const uint8_t *bytes, uint16_t received,
                                     uint8_t speed,
                                     struct xhci_usb_descriptor_facts *facts);
bool xhci_configuration_total_length(const uint8_t *bytes, uint16_t received,
                                     uint16_t *total_length);
bool xhci_validate_configuration_descriptor(
    const uint8_t *bytes, uint16_t received,
    struct xhci_usb_descriptor_facts *facts);

/* Pure M51 model helpers. They do not issue hardware traffic. */
bool xhci_usb_endpoint_id(uint8_t endpoint_address, uint8_t *endpoint_id);
bool xhci_parse_hid_configuration(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    struct xhci_hid_configuration *configuration);
bool xhci_build_set_configuration_control_td(
    struct xhci_control_td *td, uint64_t ep0_ring_physical,
    uint16_t producer_index, bool producer_cycle, uint8_t configuration_value);
bool xhci_validate_no_data_control_event(
    const struct xhci_trb *event, uint64_t ep0_ring_physical,
    uint8_t expected_slot_id, uint64_t expected_status_trb_physical);
bool xhci_build_configure_hid_context(
    void *buffer, uint32_t length, bool context_64_bytes,
    uint8_t max_slots, uint8_t slot_id, uint8_t max_ports,
    uint8_t root_port_id, uint8_t speed,
    const struct xhci_hid_configuration *configuration,
    const uint64_t ring_physical[XHCI_MAX_HID_ENDPOINTS]);
bool xhci_build_configure_endpoint_command(
    struct xhci_trb *command, uint64_t input_context_physical,
    uint8_t max_slots, uint8_t slot_id);

/* Pure M52 interrupt-transfer helpers. They do not issue hardware traffic. */
bool xhci_build_interrupt_in_trb(
    struct xhci_trb *trb, uint64_t ring_physical,
    uint16_t producer_index, bool producer_cycle,
    uint64_t buffer_physical, uint16_t length,
    uint64_t *trb_physical, uint16_t *next_producer_index,
    bool *next_producer_cycle);
bool xhci_validate_interrupt_transfer_event(
    const struct xhci_trb *event, uint64_t ring_physical,
    uint8_t expected_slot_id, uint8_t expected_endpoint_id,
    uint64_t expected_trb_physical, uint16_t requested_length,
    uint16_t *actual_length, bool *short_packet);

/* Initializes one segment-zero xHCI controller through PCI BAR0. */
bool xhci_init(struct xhci_state *state);
bool xhci_address_connected(struct xhci_state *state);
bool xhci_discover_descriptors(struct xhci_state *state);
bool xhci_configure_hid_devices(struct xhci_state *state);
bool xhci_poll_hid_reports(struct xhci_state *state, uint32_t completion_goal);
bool xhci_service_hid_reports(struct xhci_state *state);
const struct xhci_state *xhci_get_state(void);

_Static_assert(sizeof(struct xhci_trb) == XHCI_TRB_SIZE,
               "xHCI TRB must remain 16 bytes");

#endif
