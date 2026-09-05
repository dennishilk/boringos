#ifndef BORING_USB_MASS_STORAGE_H
#define BORING_USB_MASS_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/xhci.h>

#define USB_MASS_STORAGE_CLASS 0x08U
#define USB_MASS_STORAGE_SUBCLASS_SCSI 0x06U
#define USB_MASS_STORAGE_PROTOCOL_BOT 0x50U
#define USB_MASS_STORAGE_MAX_TRANSFER 4096U
#define USB_MASS_STORAGE_CBW_SIZE 31U
#define USB_MASS_STORAGE_CSW_SIZE 13U
#define USB_MASS_STORAGE_CBW_SIGNATURE 0x43425355U
#define USB_MASS_STORAGE_CSW_SIGNATURE 0x53425355U

struct usb_mass_storage_endpoint {
    uint16_t max_packet;
    uint8_t address;
    uint8_t endpoint_id;
};

struct usb_mass_storage_configuration {
    struct usb_mass_storage_endpoint bulk_in;
    struct usb_mass_storage_endpoint bulk_out;
    uint8_t configuration_value;
    uint8_t interface_number;
};

enum usb_mass_storage_flush_diagnostic {
    USB_FLUSH_NONE = 0,
    USB_FLUSH_TRANSPORT_FAILURE,
    USB_FLUSH_CSW_STATUS_FAILURE,
    USB_FLUSH_CHECK_CONDITION,
    USB_FLUSH_UNSUPPORTED_COMMAND
};

/*
 * Physical REQUEST SENSE witness field names.  The runtime emits each tag on
 * POST80 followed by the raw byte value, then emits the final classification.
 */
enum usb_mass_storage_flush_sense_witness {
    USB_FLUSH_SENSE_KEY = 1,
    USB_FLUSH_ASC,
    USB_FLUSH_ASCQ
};

struct usb_mass_storage_sense {
    uint8_t response_code;
    uint8_t sense_key;
    uint8_t asc;
    uint8_t ascq;
};

struct usb_mass_storage_stats {
    uint64_t block_count;
    uint64_t byte_capacity;
    uint32_t logical_block_size;
    uint32_t bot_commands;
    uint32_t bulk_in_transfers;
    uint32_t bulk_out_transfers;
    uint32_t read_commands;
    uint32_t write_commands;
    uint32_t fua_write_commands;
    uint32_t flush_commands;
    uint32_t short_packets;
    uint8_t flush_diagnostic;
    uint8_t flush_sense_key;
    uint8_t flush_asc;
    uint8_t flush_ascq;
    uint8_t slot_id;
    uint8_t interface_number;
    uint8_t bulk_in_address;
    uint8_t bulk_out_address;
    uint16_t bulk_in_max_packet;
    uint16_t bulk_out_max_packet;
    bool synchronize_cache_unsupported;
    bool force_unit_access_writes;
    bool flush_sense_valid;
    bool configured;
    bool registered;
};

bool usb_mass_storage_parse_configuration(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    struct usb_mass_storage_configuration *configuration);

bool usb_mass_storage_validate_csw(const uint8_t *bytes, size_t length,
                                   uint32_t expected_tag,
                                   uint32_t transfer_length,
                                   uint32_t *residue, uint8_t *status);

bool usb_mass_storage_parse_sense(
    const uint8_t *bytes, size_t length,
    struct usb_mass_storage_sense *sense_out);

bool usb_mass_storage_init(struct xhci_state *state);
const struct block_device *usb_mass_storage_get_block_device(void);
const struct usb_mass_storage_stats *usb_mass_storage_get_stats(void);
bool usb_mass_storage_flush(void);
void usb_mass_storage_cleanup(void);

#endif
