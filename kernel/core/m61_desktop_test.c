#include <boring/xhci_mixed.h>

#define virtio_blk_init m61_usb_root_init
#define virtio_blk_device m61_usb_root_device
#define block_device_find m61_block_device_find
#define boringfs_vfs_create_readonly m61_boringfs_create_root
#define xhci_configure_hid_devices xhci_configure_hid_devices_mixed
#define m37_desktop_test_finish_from_pid1 m61_base_finish_from_pid1
#include "m37_desktop_test.c"
#undef m37_desktop_test_finish_from_pid1
#undef xhci_configure_hid_devices
#undef boringfs_vfs_create_readonly
#undef block_device_find
#undef virtio_blk_device
#undef virtio_blk_init

#include <boring/block_slice.h>
#include <boring/m61_usb_layout.h>
#include <boring/usb_mass_storage.h>

const struct block_device *block_device_find(const char *name);
void m37_desktop_test_finish_from_pid1(void) __attribute__((noreturn));

static struct block_device_slice m61_root_slice;
static bool m61_root_ready;

static bool m61_name_equals(const char *left, const char *right) {
    size_t index = 0U;
    if ((left == NULL) || (right == NULL)) {
        return false;
    }
    while ((left[index] != '\0') && (right[index] != '\0')) {
        if (left[index] != right[index]) {
            return false;
        }
        ++index;
    }
    return left[index] == right[index];
}

enum virtio_blk_result m61_usb_root_init(void) {
    const struct xhci_state *published = xhci_get_state();
    struct xhci_state state;
    const struct block_device *usb0;
    const struct usb_mass_storage_stats *stats;
    enum block_device_result slice_result;

    m61_root_ready = false;
    if (published == NULL) {
        serial_write_string("m61-root: FAIL CLOSED usb0 unavailable (no xHCI state)\n");
        return VIRTIO_BLK_RESULT_NO_DEVICE;
    }
    state = *published;
    if (!usb_mass_storage_init(&state)) {
        serial_write_string("m61-root: FAIL CLOSED usb0 unavailable or unsupported\n");
        return VIRTIO_BLK_RESULT_NO_DEVICE;
    }
    usb0 = usb_mass_storage_get_block_device();
    stats = usb_mass_storage_get_stats();
    if ((usb0 == NULL) || (stats == NULL) || !stats->registered ||
        !stats->configured || (block_device_find("usb0") != usb0) ||
        usb0->read_only || (usb0->logical_block_size != M61_USB_SECTOR_SIZE)) {
        serial_write_string("m61-root: FAIL CLOSED invalid usb0 geometry/registration\n");
        return VIRTIO_BLK_RESULT_BAD_GEOMETRY;
    }
    slice_result = block_device_slice_register(
        &m61_root_slice, "m61root", usb0, M61_USB_ROOT_FIRST_LBA,
        M61_USB_ROOT_SECTORS, false);
    if (slice_result != BLOCK_DEVICE_RESULT_OK) {
        serial_write_string("m61-root: FAIL CLOSED bounded root region outside usb0\n");
        return VIRTIO_BLK_RESULT_BAD_GEOMETRY;
    }
    m61_root_ready = true;
    serial_write_string("m61-root: Mass Storage 08/06/50 usb0 registered through M21\n");
    serial_write_string("m61-root: descriptor-derived Bulk OUT=");
    serial_write_hex_u64((uint64_t)stats->bulk_out_address);
    serial_write_string(" Bulk IN=");
    serial_write_hex_u64((uint64_t)stats->bulk_in_address);
    serial_write_string("\n");
    serial_write_string("m61-root: usb0 sectors=");
    serial_write_u64(usb0->block_count);
    serial_write_string(" root first/count=");
    serial_write_u64(M61_USB_ROOT_FIRST_LBA);
    serial_write_string("/");
    serial_write_u64(M61_USB_ROOT_SECTORS);
    serial_write_string("\n");
    return VIRTIO_BLK_RESULT_OK;
}

const struct block_device *m61_block_device_find(const char *name) {
    if (m61_name_equals(name, "vblk0")) {
        return m61_root_ready ? &m61_root_slice.device : NULL;
    }
    return block_device_find(name);
}

const struct block_device *m61_usb_root_device(void) {
    return m61_root_ready ? &m61_root_slice.device : NULL;
}

enum vfs_result m61_boringfs_create_root(
    const struct block_device *device,
    uint64_t filesystem_id,
    struct boringfs_vfs **boringfs_out,
    struct boringfs_validation_error *validation_error_out) {
    enum vfs_result result;

    if (!m61_root_ready || (device != &m61_root_slice.device) ||
        device->read_only || (device->logical_block_size != 512U)) {
        serial_write_string("m61-root: FAIL CLOSED USB root slice unavailable\n");
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    result = boringfs_vfs_create_writable(device, filesystem_id,
                                          boringfs_out,
                                          validation_error_out);
    if (result != VFS_RESULT_OK) {
        serial_write_string("m61-root: FAIL CLOSED malformed/absent BoringFS root\n");
        return result;
    }
    serial_write_string("m61-root: writable BoringFS mounted from usb0 bounded region\n");
    serial_write_string("m61-root: AHCI/VirtIO/RAMFS fallback disabled\n");
    return VFS_RESULT_OK;
}

void m37_desktop_test_finish_from_pid1(void) {
    const struct usb_mass_storage_stats *stats = usb_mass_storage_get_stats();
    struct boring_input_stats input_stats;

    if (!m61_root_ready || (stats == NULL) || !stats->registered ||
        (stats->bot_commands == 0U) || (stats->bulk_in_transfers == 0U) ||
        (stats->bulk_out_transfers == 0U) || (stats->read_commands == 0U) ||
        (stats->write_commands == 0U) || (stats->flush_commands == 0U) ||
        (stats->flush_commands != stats->write_commands) ||
        !boring_input_get_stats(&input_stats) ||
        (input_stats.dropped_events != 0ULL)) {
        fail("M61 usb0 transport/flush/HID accounting");
    }
    serial_write_string("m61-root: BOT/read/write/flush=");
    serial_write_u64((uint64_t)stats->bot_commands);
    serial_write_string("/");
    serial_write_u64((uint64_t)stats->read_commands);
    serial_write_string("/");
    serial_write_u64((uint64_t)stats->write_commands);
    serial_write_string("/");
    serial_write_u64((uint64_t)stats->flush_commands);
    serial_write_string("\n");
    serial_write_string("m61-root: canonical HID dropped=0\n");
    serial_write_string("M61 USB root persistence flush complete.\n");
    m61_base_finish_from_pid1();
}
