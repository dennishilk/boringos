#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/pmm.h>
#include <boring/usb_mass_storage.h>
#include <boring/vmm.h>
#include <boring/xhci.h>

#define MSC_DMA_32BIT_LIMIT 0x100000000ULL
#define MSC_TRB_TYPE_LINK 6U
#define MSC_TRB_TYPE_PORT_STATUS_EVENT 34U
#define MSC_TRB_TYPE_SHIFT 10U
#define MSC_TRB_TYPE_MASK 0x3fU
#define MSC_TRB_CYCLE (1U << 0)
#define MSC_TRB_TOGGLE_CYCLE (1U << 1)
#define MSC_TRB_ISP (1U << 2)
#define MSC_TRB_IOC (1U << 5)
#define MSC_EVENT_ENDPOINT_SHIFT 16U
#define MSC_EVENT_ENDPOINT_MASK 0x1fU
#define MSC_EVENT_SLOT_SHIFT 24U
#define MSC_EVENT_COMPLETION_SHIFT 24U
#define MSC_EVENT_RESIDUAL_MASK 0x00ffffffU
#define MSC_RUNTIME_INTERRUPTER0 0x20U
#define MSC_EVENT_WAIT_LIMIT 30000000U
#define MSC_BULK_RING_USABLE 252U
#define MSC_CONTEXT_32_SIZE 32U
#define MSC_CONTEXT_64_SIZE 64U
#define MSC_USB_ENDPOINT_BULK 2U
#define MSC_ENDPOINT_TYPE_BULK_OUT 2U
#define MSC_ENDPOINT_TYPE_BULK_IN 6U
#define MSC_SCSI_INQUIRY 0x12U
#define MSC_SCSI_TEST_UNIT_READY 0x00U
#define MSC_SCSI_REQUEST_SENSE 0x03U
#define MSC_SCSI_READ_CAPACITY_10 0x25U
#define MSC_SCSI_READ_10 0x28U
#define MSC_SCSI_WRITE_10 0x2aU
#define MSC_SCSI_SYNCHRONIZE_CACHE_10 0x35U
#define MSC_SCSI_INQUIRY_BYTES 36U
#define MSC_SCSI_SENSE_BYTES 18U
#define MSC_SCSI_CAPACITY_BYTES 8U

struct usb_msc_runtime {
    struct xhci_state *state;
    struct xhci_addressed_device *device;
    volatile uint8_t *mmio;
    struct usb_mass_storage_configuration configuration;
    struct block_device block_device;
    struct usb_mass_storage_stats stats;
    uint64_t bulk_in_ring_physical;
    uint64_t bulk_out_ring_physical;
    uint64_t cbw_physical;
    uint64_t data_physical;
    uint64_t csw_physical;
    void *bulk_in_ring_virtual;
    void *bulk_out_ring_virtual;
    uint8_t *cbw_virtual;
    uint8_t *data_virtual;
    uint8_t *csw_virtual;
    uint16_t bulk_in_producer;
    uint16_t bulk_out_producer;
    bool bulk_in_cycle;
    bool bulk_out_cycle;
    uint32_t next_tag;
    bool initialized;
};

static struct usb_msc_runtime msc_runtime;

#if USB_MASS_STORAGE_RUNTIME && defined(BORING_M61_PHYSICAL_BREADCRUMBS)
static uint8_t msc_m61_failure_reason;

#define MSC_M61_POST(code) \
    x86_64_out8((uint16_t)0x80U, (uint8_t)(code))
#define MSC_M61_BEGIN() \
    do { msc_m61_failure_reason = 0U; } while (0)
#define MSC_M61_PROGRESS(code) \
    do { MSC_M61_POST(code); } while (0)
#define MSC_M61_FAILURE(code) \
    do { \
        msc_m61_failure_reason = (uint8_t)(code); \
        MSC_M61_POST(code); \
    } while (0)

uint8_t boring_m61_usb_mass_storage_failure_reason(void) {
    return msc_m61_failure_reason;
}
#else
#define MSC_M61_BEGIN() do { } while (0)
#define MSC_M61_PROGRESS(code) do { } while (0)
#define MSC_M61_FAILURE(code) do { } while (0)
#endif

static enum block_device_result msc_backend_read(void *context,
                                                  uint64_t first_block,
                                                  uint32_t block_count,
                                                  void *buffer);
static enum block_device_result msc_backend_write(void *context,
                                                   uint64_t first_block,
                                                   uint32_t block_count,
                                                   const void *buffer);

static const struct block_device_ops msc_block_ops = {
    .read = msc_backend_read,
    .write = msc_backend_write
};

static uint16_t little16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t little32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint32_t big32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void store_little32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void store_big16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

static void store_big32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static void bytes_zero(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;
    for (index = 0U; index < length; ++index) { bytes[index] = 0U; }
}

static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    size_t index;
    for (index = 0U; index < length; ++index) { to[index] = from[index]; }
}

static bool bulk_packet_size(uint8_t speed, uint16_t raw, uint16_t *packet) {
    uint16_t value;
    if ((packet == NULL) || ((raw & 0xf800U) != 0U)) { return false; }
    value = (uint16_t)(raw & 0x07ffU);
    if (value == 0U) { return false; }
    if (speed == 2U) {
        if (value > 64U) { return false; }
    } else if (speed == 3U) {
        if (value > 512U) { return false; }
    } else {
        return false;
    }
    *packet = value;
    return true;
}

bool usb_mass_storage_parse_configuration(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    struct usb_mass_storage_configuration *configuration) {
    struct usb_mass_storage_configuration parsed = {0};
    uint16_t total;
    uint32_t offset = 0U;
    bool current_match = false;
    bool found_interface = false;
    uint8_t current_endpoint_count = 0U;
    uint8_t expected_endpoint_count = 0U;

    if ((bytes == NULL) || (configuration == NULL) || (received < 9U) ||
        (received > XHCI_DESCRIPTOR_BUFFER_BYTES) || (bytes[0] < 9U) ||
        (bytes[1] != XHCI_USB_DESCRIPTOR_CONFIGURATION) || (bytes[5] == 0U)) {
        return false;
    }
    total = little16(&bytes[2]);
    if ((total < 9U) || (total > received) ||
        (total > XHCI_DESCRIPTOR_BUFFER_BYTES)) {
        return false;
    }
    parsed.configuration_value = bytes[5];
    while (offset < total) {
        uint8_t length;
        uint8_t type;
        if ((uint32_t)total - offset < 2U) { return false; }
        length = bytes[offset];
        type = bytes[offset + 1U];
        if ((length < 2U) || ((uint32_t)length > (uint32_t)total - offset)) {
            return false;
        }
        if (type == XHCI_USB_DESCRIPTOR_INTERFACE) {
            if (length < 9U) { return false; }
            if (current_match && (current_endpoint_count != expected_endpoint_count)) {
                return false;
            }
            current_endpoint_count = 0U;
            expected_endpoint_count = bytes[offset + 4U];
            current_match = (bytes[offset + 3U] == 0U) &&
                            (bytes[offset + 5U] == USB_MASS_STORAGE_CLASS) &&
                            (bytes[offset + 6U] == USB_MASS_STORAGE_SUBCLASS_SCSI) &&
                            (bytes[offset + 7U] == USB_MASS_STORAGE_PROTOCOL_BOT);
            if (current_match) {
                if (found_interface || (expected_endpoint_count != 2U)) {
                    return false;
                }
                found_interface = true;
                parsed.interface_number = bytes[offset + 2U];
            }
        } else if ((type == XHCI_USB_DESCRIPTOR_ENDPOINT) && current_match) {
            struct usb_mass_storage_endpoint endpoint = {0};
            uint8_t endpoint_id;
            if ((length < 7U) || (current_endpoint_count >= 2U) ||
                ((bytes[offset + 3U] & 0x03U) != MSC_USB_ENDPOINT_BULK)) {
                return false;
            }
            endpoint.address = bytes[offset + 2U];
            if (!xhci_usb_endpoint_id(endpoint.address, &endpoint_id) ||
                !bulk_packet_size(speed, little16(&bytes[offset + 4U]),
                                  &endpoint.max_packet)) {
                return false;
            }
            endpoint.endpoint_id = endpoint_id;
            if ((endpoint.address & 0x80U) != 0U) {
                if (parsed.bulk_in.address != 0U) { return false; }
                parsed.bulk_in = endpoint;
            } else {
                if (parsed.bulk_out.address != 0U) { return false; }
                parsed.bulk_out = endpoint;
            }
            ++current_endpoint_count;
        }
        offset += length;
    }
    if (current_match && (current_endpoint_count != expected_endpoint_count)) {
        return false;
    }
    if ((offset != total) || !found_interface ||
        (parsed.bulk_in.address == 0U) || (parsed.bulk_out.address == 0U) ||
        (parsed.bulk_in.endpoint_id == parsed.bulk_out.endpoint_id)) {
        return false;
    }
    *configuration = parsed;
    return true;
}

bool usb_mass_storage_validate_csw(const uint8_t *bytes, size_t length,
                                   uint32_t expected_tag,
                                   uint32_t transfer_length,
                                   uint32_t *residue, uint8_t *status) {
    uint32_t parsed_residue;
    uint8_t parsed_status;
    if ((bytes == NULL) || (residue == NULL) || (status == NULL) ||
        (length != USB_MASS_STORAGE_CSW_SIZE) ||
        (little32(&bytes[0]) != USB_MASS_STORAGE_CSW_SIGNATURE) ||
        (little32(&bytes[4]) != expected_tag)) {
        return false;
    }
    parsed_residue = little32(&bytes[8]);
    parsed_status = bytes[12];
    if ((parsed_residue > transfer_length) || (parsed_status > 1U)) {
        return false;
    }
    *residue = parsed_residue;
    *status = parsed_status;
    return true;
}

static void mmio_write32(volatile uint8_t *base, uint32_t offset,
                         uint32_t value) {
    *(volatile uint32_t *)(volatile void *)(base + offset) = value;
}

static void mmio_write64(volatile uint8_t *base, uint32_t offset,
                         uint64_t value) {
    mmio_write32(base, offset, (uint32_t)value);
    mmio_write32(base, offset + 4U, (uint32_t)(value >> 32U));
}

static void barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

static bool dma_frame_allocate(uint64_t *physical, void **virtual_address) {
    if ((physical == NULL) || (virtual_address == NULL) ||
        !pmm_alloc_frame_in_range(0ULL, MSC_DMA_32BIT_LIMIT, physical)) {
        return false;
    }
    if ((*physical >= MSC_DMA_32BIT_LIMIT) ||
        ((*physical & (PMM_PAGE_SIZE - 1ULL)) != 0ULL) ||
        !vmm_pmm_frame_to_hhdm(*physical, virtual_address)) {
        (void)pmm_free_frame(*physical);
        *physical = 0ULL;
        *virtual_address = NULL;
        return false;
    }
    bytes_zero(*virtual_address, (size_t)PMM_PAGE_SIZE);
    return true;
}

static void dma_frame_free(uint64_t *physical, void **virtual_address) {
    if ((physical != NULL) && (*physical != 0ULL)) {
        (void)pmm_free_frame(*physical);
        *physical = 0ULL;
    }
    if (virtual_address != NULL) { *virtual_address = NULL; }
}

static uint64_t consumed_events(const struct xhci_state *state) {
    uint64_t total;
    uint8_t index;
    total = (uint64_t)state->command_completions +
            (uint64_t)state->port_events_consumed;
    for (index = 0U; index < state->addressed_count; ++index) {
        total += (uint64_t)state->addressed[index].transfer_events;
    }
    return total;
}

static bool take_event(struct xhci_trb *event, uint16_t *next_index) {
    volatile struct xhci_trb *ring = NULL;
    void *ring_virtual = NULL;
    uint32_t attempt;
    if ((event == NULL) || (next_index == NULL) ||
        (msc_runtime.state == NULL) || (msc_runtime.mmio == NULL) ||
        !vmm_pmm_frame_to_hhdm(msc_runtime.state->event_ring_physical,
                               &ring_virtual)) {
        return false;
    }
    ring = (volatile struct xhci_trb *)ring_virtual;
    for (attempt = 0U; attempt < MSC_EVENT_WAIT_LIMIT; ++attempt) {
        const uint64_t consumed = consumed_events(msc_runtime.state);
        const uint16_t index = (uint16_t)(consumed % XHCI_EVENT_RING_TRBS);
        const bool cycle = (((consumed / XHCI_EVENT_RING_TRBS) & 1ULL) == 0ULL);
        const uint32_t control = ring[index].control;
        if (((control & MSC_TRB_CYCLE) != 0U) != cycle) {
            x86_64_pause();
            continue;
        }
        event->parameter = ring[index].parameter;
        event->status = ring[index].status;
        event->control = control;
        *next_index = (uint16_t)(index + 1U);
        if (*next_index == XHCI_EVENT_RING_TRBS) { *next_index = 0U; }
        return true;
    }
    return false;
}

static bool commit_event(uint16_t next_index) {
    if ((msc_runtime.state == NULL) || (msc_runtime.mmio == NULL) ||
        (next_index >= XHCI_EVENT_RING_TRBS)) {
        return false;
    }
    barrier();
    mmio_write64(msc_runtime.mmio,
                 msc_runtime.state->capabilities.runtime_offset +
                     MSC_RUNTIME_INTERRUPTER0 + 0x18U,
                 (msc_runtime.state->event_ring_physical +
                  ((uint64_t)next_index * XHCI_TRB_SIZE)) |
                     (1ULL << 3U));
    return true;
}

static bool consume_port_event(const struct xhci_trb *event) {
    const uint8_t port = (uint8_t)(event->parameter >> 24U);
    const uint8_t completion = (uint8_t)(event->status >> 24U);
    if ((port == 0U) || (port > msc_runtime.state->capabilities.max_ports) ||
        (completion != XHCI_COMPLETION_SUCCESS) ||
        (msc_runtime.state->port_events_consumed == UINT32_MAX)) {
        return false;
    }
    ++msc_runtime.state->port_events_consumed;
    return true;
}

static bool wait_control_status(struct xhci_addressed_device *device,
                                uint64_t expected_status) {
    uint32_t attempt;
    for (attempt = 0U; attempt < MSC_EVENT_WAIT_LIMIT; ++attempt) {
        struct xhci_trb event;
        uint16_t next_index;
        uint8_t type;
        if (!take_event(&event, &next_index)) { return false; }
        type = (uint8_t)((event.control >> MSC_TRB_TYPE_SHIFT) & MSC_TRB_TYPE_MASK);
        if (type == MSC_TRB_TYPE_PORT_STATUS_EVENT) {
            if (!consume_port_event(&event) || !commit_event(next_index)) {
                return false;
            }
            continue;
        }
        if (!xhci_validate_no_data_control_event(
                &event, device->ep0_ring_physical, device->slot_id,
                expected_status) || (device->transfer_events == UINT32_MAX)) {
            return false;
        }
        ++device->transfer_events;
        return commit_event(next_index);
    }
    return false;
}

static bool set_configuration(struct xhci_addressed_device *device,
                              uint8_t configuration_value) {
    void *ring_virtual = NULL;
    volatile struct xhci_trb *ring;
    struct xhci_control_td td;
    uint16_t start;
    uint32_t doorbell;
    if ((device == NULL) || (configuration_value == 0U) ||
        !vmm_pmm_frame_to_hhdm(device->ep0_ring_physical, &ring_virtual) ||
        !xhci_build_set_configuration_control_td(
            &td, device->ep0_ring_physical, device->ep0_producer_index,
            device->ep0_producer_cycle, configuration_value)) {
        return false;
    }
    ring = (volatile struct xhci_trb *)ring_virtual;
    start = device->ep0_producer_index;
    ring[start] = td.setup;
    ring[(uint16_t)(start + 1U)] = td.status;
    if (start == XHCI_EP0_RING_USABLE - 2U) {
        ring[XHCI_EP0_RING_USABLE].parameter = device->ep0_ring_physical;
        ring[XHCI_EP0_RING_USABLE].status = 0U;
        ring[XHCI_EP0_RING_USABLE].control =
            ((uint32_t)MSC_TRB_TYPE_LINK << MSC_TRB_TYPE_SHIFT) |
            MSC_TRB_TOGGLE_CYCLE |
            (device->ep0_producer_cycle ? MSC_TRB_CYCLE : 0U);
    }
    device->ep0_producer_index = td.next_producer_index;
    device->ep0_producer_cycle = td.next_producer_cycle;
    barrier();
    doorbell = msc_runtime.state->capabilities.doorbell_offset +
               ((uint32_t)device->slot_id * 4U);
    if (doorbell > XHCI_MMIO_WINDOW_SIZE - 4U) { return false; }
    mmio_write32(msc_runtime.mmio, doorbell, 1U);
    if (!wait_control_status(device, td.status_physical) ||
        (device->set_configuration_completions == UINT32_MAX)) {
        return false;
    }
    ++device->set_configuration_completions;
    return true;
}

static void context_write32(uint8_t *context, uint32_t offset,
                            uint32_t value) {
    context[offset] = (uint8_t)value;
    context[offset + 1U] = (uint8_t)(value >> 8U);
    context[offset + 2U] = (uint8_t)(value >> 16U);
    context[offset + 3U] = (uint8_t)(value >> 24U);
}

static bool build_bulk_context(void *buffer, uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    const uint32_t context_size =
        msc_runtime.state->capabilities.context_64_bytes ?
        MSC_CONTEXT_64_SIZE : MSC_CONTEXT_32_SIZE;
    const uint8_t in_dci = msc_runtime.configuration.bulk_in.endpoint_id;
    const uint8_t out_dci = msc_runtime.configuration.bulk_out.endpoint_id;
    const uint8_t highest = (in_dci > out_dci) ? in_dci : out_dci;
    const uint32_t required = ((uint32_t)highest + 2U) * context_size;
    uint8_t *slot;
    uint8_t *ep_in;
    uint8_t *ep_out;
    if ((bytes == NULL) || (required > length) || (highest > 31U) ||
        (in_dci <= 1U) || (out_dci <= 1U)) {
        return false;
    }
    bytes_zero(bytes, required);
    context_write32(bytes, 4U, 1U | (1U << in_dci) | (1U << out_dci));
    slot = bytes + context_size;
    context_write32(slot, 0U,
                    ((uint32_t)msc_runtime.device->speed << 20U) |
                    ((uint32_t)highest << 27U));
    context_write32(slot, 4U,
                    (uint32_t)msc_runtime.device->root_port_id << 16U);

    ep_out = bytes + (((uint32_t)out_dci + 1U) * context_size);
    context_write32(ep_out, 4U,
                    (3U << 1U) | (MSC_ENDPOINT_TYPE_BULK_OUT << 3U) |
                    ((uint32_t)msc_runtime.configuration.bulk_out.max_packet << 16U));
    context_write32(ep_out, 8U,
                    (uint32_t)(msc_runtime.bulk_out_ring_physical | 1ULL));
    context_write32(ep_out, 12U,
                    (uint32_t)((msc_runtime.bulk_out_ring_physical | 1ULL) >> 32U));
    context_write32(ep_out, 16U,
                    (uint32_t)msc_runtime.configuration.bulk_out.max_packet);

    ep_in = bytes + (((uint32_t)in_dci + 1U) * context_size);
    context_write32(ep_in, 4U,
                    (3U << 1U) | (MSC_ENDPOINT_TYPE_BULK_IN << 3U) |
                    ((uint32_t)msc_runtime.configuration.bulk_in.max_packet << 16U));
    context_write32(ep_in, 8U,
                    (uint32_t)(msc_runtime.bulk_in_ring_physical | 1ULL));
    context_write32(ep_in, 12U,
                    (uint32_t)((msc_runtime.bulk_in_ring_physical | 1ULL) >> 32U));
    context_write32(ep_in, 16U,
                    (uint32_t)msc_runtime.configuration.bulk_in.max_packet);
    return true;
}

static bool wait_command(uint64_t expected_command) {
    uint32_t attempt;
    for (attempt = 0U; attempt < MSC_EVENT_WAIT_LIMIT; ++attempt) {
        struct xhci_trb event;
        uint16_t next_index;
        uint8_t completed_slot = 0U;
        uint8_t type;
        if (!take_event(&event, &next_index)) { return false; }
        type = (uint8_t)((event.control >> MSC_TRB_TYPE_SHIFT) & MSC_TRB_TYPE_MASK);
        if (type == MSC_TRB_TYPE_PORT_STATUS_EVENT) {
            if (!consume_port_event(&event) || !commit_event(next_index)) {
                return false;
            }
            continue;
        }
        if (!xhci_validate_command_completion(
                &event, expected_command, msc_runtime.state->capabilities.max_slots,
                &completed_slot) ||
            (completed_slot != msc_runtime.device->slot_id) ||
            (msc_runtime.state->command_completions == UINT32_MAX)) {
            return false;
        }
        ++msc_runtime.state->command_completions;
        return commit_event(next_index);
    }
    return false;
}

static bool configure_bulk_endpoints(void) {
    void *input_virtual = NULL;
    void *command_virtual = NULL;
    volatile struct xhci_trb *command_ring;
    struct xhci_trb command;
    uint64_t command_physical;
    uint32_t completed;
    uint16_t index;
    bool cycle;
    if (!vmm_pmm_frame_to_hhdm(msc_runtime.device->input_context_physical,
                               &input_virtual) ||
        !vmm_pmm_frame_to_hhdm(msc_runtime.state->command_ring_physical,
                               &command_virtual) ||
        !build_bulk_context(input_virtual, (uint32_t)PMM_PAGE_SIZE) ||
        !xhci_build_configure_endpoint_command(
            &command, msc_runtime.device->input_context_physical,
            msc_runtime.state->capabilities.max_slots,
            msc_runtime.device->slot_id)) {
        return false;
    }
    completed = msc_runtime.state->command_completions;
    index = (uint16_t)(completed % XHCI_COMMAND_RING_USABLE);
    cycle = (((completed / XHCI_COMMAND_RING_USABLE) & 1U) == 0U);
    command_ring = (volatile struct xhci_trb *)command_virtual;
    command.control |= cycle ? MSC_TRB_CYCLE : 0U;
    command_ring[index] = command;
    command_physical = msc_runtime.state->command_ring_physical +
                       ((uint64_t)index * XHCI_TRB_SIZE);
    if (index == XHCI_COMMAND_RING_USABLE - 1U) {
        command_ring[XHCI_COMMAND_RING_USABLE].parameter =
            msc_runtime.state->command_ring_physical;
        command_ring[XHCI_COMMAND_RING_USABLE].status = 0U;
        command_ring[XHCI_COMMAND_RING_USABLE].control =
            ((uint32_t)MSC_TRB_TYPE_LINK << MSC_TRB_TYPE_SHIFT) |
            MSC_TRB_TOGGLE_CYCLE | (cycle ? MSC_TRB_CYCLE : 0U);
    }
    barrier();
    mmio_write32(msc_runtime.mmio,
                 msc_runtime.state->capabilities.doorbell_offset, 0U);
    if (!wait_command(command_physical) ||
        (msc_runtime.device->configure_endpoint_completions == UINT32_MAX)) {
        return false;
    }
    ++msc_runtime.device->configure_endpoint_completions;
    msc_runtime.device->device_configured = true;
    return true;
}

static bool wait_bulk(uint8_t endpoint_id, uint64_t ring_physical,
                      uint64_t expected_trb, uint32_t requested,
                      uint32_t *actual, bool *short_packet) {
    uint32_t attempt;
    for (attempt = 0U; attempt < MSC_EVENT_WAIT_LIMIT; ++attempt) {
        struct xhci_trb event;
        uint16_t next_index;
        uint8_t type;
        enum xhci_shared_transfer_owner owner = XHCI_SHARED_TRANSFER_REJECT;
        uint32_t storage_actual = 0U;
        bool storage_short = false;
        if (!take_event(&event, &next_index)) { return false; }
        type = (uint8_t)((event.control >> MSC_TRB_TYPE_SHIFT) & MSC_TRB_TYPE_MASK);
        if (type == MSC_TRB_TYPE_PORT_STATUS_EVENT) {
            if (!consume_port_event(&event) || !commit_event(next_index)) {
                return false;
            }
            continue;
        }
        if (!xhci_classify_shared_transfer_event(
                msc_runtime.state, &event, msc_runtime.device->slot_id,
                endpoint_id, ring_physical, MSC_BULK_RING_USABLE,
                expected_trb, requested, &owner, &storage_actual,
                &storage_short)) {
            return false;
        }
        if (owner == XHCI_SHARED_TRANSFER_STORAGE) {
            if (msc_runtime.device->transfer_events == UINT32_MAX) {
                return false;
            }
            *actual = storage_actual;
            *short_packet = storage_short;
            ++msc_runtime.device->transfer_events;
            return commit_event(next_index);
        }
        if (owner == XHCI_SHARED_TRANSFER_HID) {
            if (!xhci_consume_hid_transfer_event(msc_runtime.state, &event,
                                                 true) ||
                !commit_event(next_index)) {
                return false;
            }
            continue;
        }
        return false;
    }
    return false;
}

static bool bulk_transfer(bool direction_in, uint64_t buffer_physical,
                          uint32_t length, uint32_t *actual) {
    volatile struct xhci_trb *ring;
    uint64_t ring_physical;
    uint16_t *producer;
    bool *producer_cycle;
    const struct usb_mass_storage_endpoint *endpoint;
    struct xhci_trb trb = {0};
    uint16_t index;
    uint64_t trb_physical;
    bool short_packet = false;
    uint32_t doorbell;
    if ((actual == NULL) || (buffer_physical == 0ULL) ||
        (buffer_physical >= MSC_DMA_32BIT_LIMIT) || (length == 0U) ||
        (length > USB_MASS_STORAGE_MAX_TRANSFER)) {
        return false;
    }
    if (direction_in) {
        ring = (volatile struct xhci_trb *)msc_runtime.bulk_in_ring_virtual;
        ring_physical = msc_runtime.bulk_in_ring_physical;
        producer = &msc_runtime.bulk_in_producer;
        producer_cycle = &msc_runtime.bulk_in_cycle;
        endpoint = &msc_runtime.configuration.bulk_in;
    } else {
        ring = (volatile struct xhci_trb *)msc_runtime.bulk_out_ring_virtual;
        ring_physical = msc_runtime.bulk_out_ring_physical;
        producer = &msc_runtime.bulk_out_producer;
        producer_cycle = &msc_runtime.bulk_out_cycle;
        endpoint = &msc_runtime.configuration.bulk_out;
    }
    if ((ring == NULL) || (*producer >= MSC_BULK_RING_USABLE)) { return false; }
    index = *producer;
    trb.parameter = buffer_physical;
    trb.status = length;
    trb.control = ((uint32_t)XHCI_TRB_TYPE_NORMAL << MSC_TRB_TYPE_SHIFT) |
                  MSC_TRB_IOC | MSC_TRB_ISP |
                  (*producer_cycle ? MSC_TRB_CYCLE : 0U);
    ring[index] = trb;
    trb_physical = ring_physical + ((uint64_t)index * XHCI_TRB_SIZE);
    if (index == MSC_BULK_RING_USABLE - 1U) {
        ring[MSC_BULK_RING_USABLE].parameter = ring_physical;
        ring[MSC_BULK_RING_USABLE].status = 0U;
        ring[MSC_BULK_RING_USABLE].control =
            ((uint32_t)MSC_TRB_TYPE_LINK << MSC_TRB_TYPE_SHIFT) |
            MSC_TRB_TOGGLE_CYCLE |
            (*producer_cycle ? MSC_TRB_CYCLE : 0U);
        *producer = 0U;
        *producer_cycle = !*producer_cycle;
    } else {
        *producer = (uint16_t)(index + 1U);
    }
    barrier();
    doorbell = msc_runtime.state->capabilities.doorbell_offset +
               ((uint32_t)msc_runtime.device->slot_id * 4U);
    if (doorbell > XHCI_MMIO_WINDOW_SIZE - 4U) { return false; }
    mmio_write32(msc_runtime.mmio, doorbell, endpoint->endpoint_id);
    if (!wait_bulk(endpoint->endpoint_id, ring_physical, trb_physical,
                   length, actual, &short_packet)) {
        return false;
    }
    if (short_packet) {
        if (msc_runtime.stats.short_packets == UINT32_MAX) { return false; }
        ++msc_runtime.stats.short_packets;
    }
    if (direction_in) {
        if (msc_runtime.stats.bulk_in_transfers == UINT32_MAX) { return false; }
        ++msc_runtime.stats.bulk_in_transfers;
    } else {
        if (short_packet || (*actual != length) ||
            (msc_runtime.stats.bulk_out_transfers == UINT32_MAX)) {
            return false;
        }
        ++msc_runtime.stats.bulk_out_transfers;
    }
    return true;
}

static bool bot_execute(const uint8_t *cdb, uint8_t cdb_length,
                        bool direction_in, void *data, uint32_t data_length,
                        uint32_t *data_actual, uint8_t *command_status) {
    uint32_t tag;
    uint32_t actual;
    uint32_t residue = 0U;
    uint8_t status = 0xffU;
    size_t index;
    if ((cdb == NULL) || (cdb_length == 0U) || (cdb_length > 16U) ||
        (data_actual == NULL) || (command_status == NULL) ||
        (data_length > USB_MASS_STORAGE_MAX_TRANSFER) ||
        ((data_length != 0U) && (data == NULL)) ||
        (msc_runtime.next_tag == 0U)) {
        return false;
    }
    tag = msc_runtime.next_tag;
    ++msc_runtime.next_tag;
    if (msc_runtime.next_tag == 0U) { msc_runtime.next_tag = 1U; }

    bytes_zero(msc_runtime.cbw_virtual, (size_t)PMM_PAGE_SIZE);
    store_little32(&msc_runtime.cbw_virtual[0], USB_MASS_STORAGE_CBW_SIGNATURE);
    store_little32(&msc_runtime.cbw_virtual[4], tag);
    store_little32(&msc_runtime.cbw_virtual[8], data_length);
    msc_runtime.cbw_virtual[12] = direction_in ? 0x80U : 0U;
    msc_runtime.cbw_virtual[13] = 0U;
    msc_runtime.cbw_virtual[14] = cdb_length;
    for (index = 0U; index < (size_t)cdb_length; ++index) {
        msc_runtime.cbw_virtual[15U + index] = cdb[index];
    }
    if (!bulk_transfer(false, msc_runtime.cbw_physical,
                       USB_MASS_STORAGE_CBW_SIZE, &actual) ||
        (actual != USB_MASS_STORAGE_CBW_SIZE)) {
        return false;
    }

    *data_actual = 0U;
    if (data_length != 0U) {
        bytes_zero(msc_runtime.data_virtual, (size_t)PMM_PAGE_SIZE);
        if (!direction_in) {
            bytes_copy(msc_runtime.data_virtual, data, (size_t)data_length);
        }
        if (!bulk_transfer(direction_in, msc_runtime.data_physical,
                           data_length, &actual)) {
            return false;
        }
        *data_actual = actual;
        if (!direction_in && (actual != data_length)) { return false; }
    }

    bytes_zero(msc_runtime.csw_virtual, (size_t)PMM_PAGE_SIZE);
    if (!bulk_transfer(true, msc_runtime.csw_physical,
                       USB_MASS_STORAGE_CSW_SIZE, &actual) ||
        !usb_mass_storage_validate_csw(
            msc_runtime.csw_virtual, (size_t)actual, tag, data_length,
            &residue, &status) ||
        (residue != (data_length - *data_actual))) {
        return false;
    }
    if (direction_in && (*data_actual != 0U)) {
        bytes_copy(data, msc_runtime.data_virtual, (size_t)*data_actual);
    }
    if (msc_runtime.stats.bot_commands == UINT32_MAX) { return false; }
    ++msc_runtime.stats.bot_commands;
    *command_status = status;
    return true;
}

static bool scsi_no_data(const uint8_t *cdb, uint8_t cdb_length,
                         uint8_t *status) {
    uint32_t actual = 0U;
    return bot_execute(cdb, cdb_length, false, NULL, 0U, &actual, status) &&
           (actual == 0U);
}

static bool scsi_inquiry(void) {
    uint8_t cdb[6] = { MSC_SCSI_INQUIRY, 0U, 0U, 0U,
                       MSC_SCSI_INQUIRY_BYTES, 0U };
    uint8_t inquiry[MSC_SCSI_INQUIRY_BYTES];
    uint32_t actual = 0U;
    uint8_t status = 0xffU;
    return bot_execute(cdb, 6U, true, inquiry, MSC_SCSI_INQUIRY_BYTES,
                       &actual, &status) &&
           (status == 0U) && (actual >= 5U) &&
           ((inquiry[0] & 0x1fU) == 0U);
}

static bool scsi_test_unit_ready(void) {
    uint8_t cdb[6] = { MSC_SCSI_TEST_UNIT_READY, 0U, 0U, 0U, 0U, 0U };
    uint8_t status = 0xffU;
    if (!scsi_no_data(cdb, 6U, &status)) { return false; }
    if (status == 0U) { return true; }
    if (status == 1U) {
        uint8_t sense_cdb[6] = { MSC_SCSI_REQUEST_SENSE, 0U, 0U, 0U,
                                 MSC_SCSI_SENSE_BYTES, 0U };
        uint8_t sense[MSC_SCSI_SENSE_BYTES];
        uint32_t actual = 0U;
        uint8_t sense_status = 0xffU;
        if (!bot_execute(sense_cdb, 6U, true, sense, MSC_SCSI_SENSE_BYTES,
                         &actual, &sense_status) ||
            (sense_status != 0U) || (actual < 8U)) {
            return false;
        }
        status = 0xffU;
        return scsi_no_data(cdb, 6U, &status) && (status == 0U);
    }
    return false;
}

static bool scsi_read_capacity(void) {
    uint8_t cdb[10] = { MSC_SCSI_READ_CAPACITY_10, 0U, 0U, 0U, 0U,
                        0U, 0U, 0U, 0U, 0U };
    uint8_t capacity[MSC_SCSI_CAPACITY_BYTES];
    uint32_t actual = 0U;
    uint8_t status = 0xffU;
    uint32_t last_lba;
    uint32_t block_size;
    uint64_t blocks;
    uint64_t bytes;
    if (!bot_execute(cdb, 10U, true, capacity, MSC_SCSI_CAPACITY_BYTES,
                     &actual, &status) || (status != 0U) ||
        (actual != MSC_SCSI_CAPACITY_BYTES)) {
        return false;
    }
    last_lba = big32(&capacity[0]);
    block_size = big32(&capacity[4]);
    if ((last_lba == UINT32_MAX) || (block_size < 512U) ||
        (block_size > 4096U) || ((block_size & (block_size - 1U)) != 0U) ||
        (block_size > USB_MASS_STORAGE_MAX_TRANSFER)) {
        return false;
    }
    blocks = (uint64_t)last_lba + 1ULL;
    if (blocks > (UINT64_MAX / (uint64_t)block_size)) { return false; }
    bytes = blocks * (uint64_t)block_size;
    if (bytes == 0ULL) { return false; }
    msc_runtime.stats.block_count = blocks;
    msc_runtime.stats.logical_block_size = block_size;
    msc_runtime.stats.byte_capacity = bytes;
    return true;
}

static bool scsi_rw10(bool write, uint32_t lba, uint16_t blocks,
                      void *buffer) {
    uint8_t cdb[10] = {0U};
    uint32_t bytes;
    uint32_t actual = 0U;
    uint8_t status = 0xffU;
    if ((blocks == 0U) || (buffer == NULL) ||
        ((uint32_t)blocks > (USB_MASS_STORAGE_MAX_TRANSFER /
                            msc_runtime.stats.logical_block_size))) {
        return false;
    }
    bytes = (uint32_t)blocks * msc_runtime.stats.logical_block_size;
    cdb[0] = write ? MSC_SCSI_WRITE_10 : MSC_SCSI_READ_10;
    store_big32(&cdb[2], lba);
    store_big16(&cdb[7], blocks);
    if (!bot_execute(cdb, 10U, !write, buffer, bytes, &actual, &status) ||
        (status != 0U) || (actual != bytes)) {
        return false;
    }
    if (write) {
        if (msc_runtime.stats.write_commands == UINT32_MAX) { return false; }
        ++msc_runtime.stats.write_commands;
    } else {
        if (msc_runtime.stats.read_commands == UINT32_MAX) { return false; }
        ++msc_runtime.stats.read_commands;
    }
    return true;
}

bool usb_mass_storage_flush(void) {
    uint8_t cdb[10] = { MSC_SCSI_SYNCHRONIZE_CACHE_10, 0U, 0U, 0U, 0U,
                        0U, 0U, 0U, 0U, 0U };
    uint8_t status = 0xffU;
    if (!msc_runtime.initialized || !scsi_no_data(cdb, 10U, &status) ||
        (status != 0U) || (msc_runtime.stats.flush_commands == UINT32_MAX)) {
        return false;
    }
    ++msc_runtime.stats.flush_commands;
    return true;
}

static enum block_device_result msc_backend_read(void *context,
                                                  uint64_t first_block,
                                                  uint32_t block_count,
                                                  void *buffer) {
    struct usb_msc_runtime *runtime = (struct usb_msc_runtime *)context;
    const uint32_t max_blocks = USB_MASS_STORAGE_MAX_TRANSFER /
                                runtime->stats.logical_block_size;
    if ((runtime != &msc_runtime) || !runtime->initialized ||
        (buffer == NULL) || (block_count == 0U) ||
        (block_count > max_blocks) || (first_block > UINT32_MAX)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }
    return scsi_rw10(false, (uint32_t)first_block, (uint16_t)block_count,
                     buffer) ? BLOCK_DEVICE_RESULT_OK :
                               BLOCK_DEVICE_RESULT_IO_ERROR;
}

static enum block_device_result msc_backend_write(void *context,
                                                   uint64_t first_block,
                                                   uint32_t block_count,
                                                   const void *buffer) {
    struct usb_msc_runtime *runtime = (struct usb_msc_runtime *)context;
    const uint32_t max_blocks = USB_MASS_STORAGE_MAX_TRANSFER /
                                runtime->stats.logical_block_size;
    if ((runtime != &msc_runtime) || !runtime->initialized ||
        (buffer == NULL) || (block_count == 0U) ||
        (block_count > max_blocks) || (first_block > UINT32_MAX)) {
        return BLOCK_DEVICE_RESULT_INVALID_ARGUMENT;
    }
    if (!scsi_rw10(true, (uint32_t)first_block, (uint16_t)block_count,
                   (void *)(uintptr_t)buffer) ||
        !usb_mass_storage_flush()) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }
    return BLOCK_DEVICE_RESULT_OK;
}

static bool allocate_transport(void) {
    struct xhci_trb *ring;
    if (!dma_frame_allocate(&msc_runtime.bulk_in_ring_physical,
                            &msc_runtime.bulk_in_ring_virtual) ||
        !dma_frame_allocate(&msc_runtime.bulk_out_ring_physical,
                            &msc_runtime.bulk_out_ring_virtual) ||
        !dma_frame_allocate(&msc_runtime.cbw_physical,
                            (void **)&msc_runtime.cbw_virtual) ||
        !dma_frame_allocate(&msc_runtime.data_physical,
                            (void **)&msc_runtime.data_virtual) ||
        !dma_frame_allocate(&msc_runtime.csw_physical,
                            (void **)&msc_runtime.csw_virtual)) {
        return false;
    }
    ring = (struct xhci_trb *)msc_runtime.bulk_in_ring_virtual;
    ring[MSC_BULK_RING_USABLE].parameter = msc_runtime.bulk_in_ring_physical;
    ring[MSC_BULK_RING_USABLE].control =
        ((uint32_t)MSC_TRB_TYPE_LINK << MSC_TRB_TYPE_SHIFT) |
        MSC_TRB_TOGGLE_CYCLE | MSC_TRB_CYCLE;
    ring = (struct xhci_trb *)msc_runtime.bulk_out_ring_virtual;
    ring[MSC_BULK_RING_USABLE].parameter = msc_runtime.bulk_out_ring_physical;
    ring[MSC_BULK_RING_USABLE].control =
        ((uint32_t)MSC_TRB_TYPE_LINK << MSC_TRB_TYPE_SHIFT) |
        MSC_TRB_TOGGLE_CYCLE | MSC_TRB_CYCLE;
    msc_runtime.bulk_in_producer = 0U;
    msc_runtime.bulk_out_producer = 0U;
    msc_runtime.bulk_in_cycle = true;
    msc_runtime.bulk_out_cycle = true;
    msc_runtime.next_tag = 1U;
    return true;
}

static bool find_mass_storage_device(void) {
    uint8_t index;
    uint8_t found = 0U;
    for (index = 0U; index < msc_runtime.state->addressed_count; ++index) {
        struct xhci_addressed_device *device =
            &msc_runtime.state->addressed[index];
        void *descriptor_virtual = NULL;
        struct usb_mass_storage_configuration configuration;
        if (!device->addressed || !device->descriptors_ready ||
            (device->descriptor_buffer_physical == 0ULL) ||
            !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                                   &descriptor_virtual)) {
            return false;
        }
        if (!usb_mass_storage_parse_configuration(
                (const uint8_t *)descriptor_virtual,
                device->descriptors.configuration_length,
                device->speed, &configuration)) {
            continue;
        }
        ++found;
        if (found != 1U) { return false; }
        msc_runtime.device = device;
        msc_runtime.configuration = configuration;
    }
    return found == 1U;
}

bool usb_mass_storage_init(struct xhci_state *state) {
    volatile void *mapping = NULL;
    enum block_device_result registration;

    MSC_M61_BEGIN();
    MSC_M61_PROGRESS(0xF1U); /* real USB mass-storage function entered */
    if ((state == NULL) || !state->controller_running ||
        (state->addressed_count == 0U) || msc_runtime.initialized) {
        MSC_M61_FAILURE(0xF9U); /* invalid/unready controller runtime */
        return false;
    }
    MSC_M61_PROGRESS(0xF2U); /* controller state accepted */
    bytes_zero(&msc_runtime, sizeof(msc_runtime));
    msc_runtime.state = state;

    MSC_M61_PROGRESS(0xF3U); /* storage discovery + MMIO mapping */
    if (!find_mass_storage_device() || (msc_runtime.device == NULL) ||
        msc_runtime.device->device_configured ||
        !vmm_map_mmio_region(state->mmio_physical, XHCI_MMIO_WINDOW_SIZE,
                             &mapping)) {
        MSC_M61_FAILURE(0xFAU); /* storage discovery/MMIO failed */
        usb_mass_storage_cleanup();
        return false;
    }
    msc_runtime.mmio = (volatile uint8_t *)mapping;

    MSC_M61_PROGRESS(0xF4U); /* transport + USB config + bulk endpoints */
    if (!allocate_transport() ||
        !set_configuration(msc_runtime.device,
                           msc_runtime.configuration.configuration_value) ||
        !configure_bulk_endpoints()) {
        MSC_M61_FAILURE(0xFBU); /* transport/config/bulk setup failed */
        usb_mass_storage_cleanup();
        return false;
    }
    msc_runtime.stats.slot_id = msc_runtime.device->slot_id;
    msc_runtime.stats.interface_number = msc_runtime.configuration.interface_number;
    msc_runtime.stats.bulk_in_address = msc_runtime.configuration.bulk_in.address;
    msc_runtime.stats.bulk_out_address = msc_runtime.configuration.bulk_out.address;
    msc_runtime.stats.bulk_in_max_packet = msc_runtime.configuration.bulk_in.max_packet;
    msc_runtime.stats.bulk_out_max_packet = msc_runtime.configuration.bulk_out.max_packet;
    msc_runtime.stats.configured = true;
    msc_runtime.initialized = true;

    MSC_M61_PROGRESS(0xF5U); /* SCSI inquiry/TUR/capacity */
    if (!scsi_inquiry() || !scsi_test_unit_ready() || !scsi_read_capacity()) {
        MSC_M61_FAILURE(0xFCU); /* SCSI initialization failed */
        usb_mass_storage_cleanup();
        return false;
    }
    msc_runtime.block_device.name = "usb0";
    msc_runtime.block_device.logical_block_size =
        msc_runtime.stats.logical_block_size;
    msc_runtime.block_device.block_count = msc_runtime.stats.block_count;
    msc_runtime.block_device.read_only = false;
    msc_runtime.block_device.context = &msc_runtime;
    msc_runtime.block_device.ops = &msc_block_ops;

    MSC_M61_PROGRESS(0xF6U); /* expose usb0 through block-device layer */
    registration = block_device_register(&msc_runtime.block_device);
    if (registration != BLOCK_DEVICE_RESULT_OK) {
        MSC_M61_FAILURE(0xFDU); /* usb0 registration failed */
        usb_mass_storage_cleanup();
        return false;
    }
    msc_runtime.stats.registered = true;
    return true;
}

const struct block_device *usb_mass_storage_get_block_device(void) {
    return msc_runtime.stats.registered ? &msc_runtime.block_device : NULL;
}

const struct usb_mass_storage_stats *usb_mass_storage_get_stats(void) {
    return msc_runtime.initialized ? &msc_runtime.stats : NULL;
}

void usb_mass_storage_cleanup(void) {
    if (msc_runtime.mmio != NULL) {
        (void)vmm_unmap_mmio_region((volatile void *)msc_runtime.mmio,
                                    XHCI_MMIO_WINDOW_SIZE);
        msc_runtime.mmio = NULL;
    }
    dma_frame_free(&msc_runtime.csw_physical,
                   (void **)&msc_runtime.csw_virtual);
    dma_frame_free(&msc_runtime.data_physical,
                   (void **)&msc_runtime.data_virtual);
    dma_frame_free(&msc_runtime.cbw_physical,
                   (void **)&msc_runtime.cbw_virtual);
    dma_frame_free(&msc_runtime.bulk_out_ring_physical,
                   &msc_runtime.bulk_out_ring_virtual);
    dma_frame_free(&msc_runtime.bulk_in_ring_physical,
                   &msc_runtime.bulk_in_ring_virtual);
    msc_runtime.initialized = false;
    msc_runtime.stats.configured = false;
    msc_runtime.stats.registered = false;
}
