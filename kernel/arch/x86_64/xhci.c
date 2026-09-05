#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/pci.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/vmm.h>
#include <boring/xhci.h>

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/io.h>

enum xhci_m61_post_code {
    XHCI_M61_RINGS_FALSE_SCRATCHPAD_UNSUPPORTED = 0x40,
    XHCI_M61_RINGS_FALSE_DCBAA_PMM = 0x41,
    XHCI_M61_RINGS_FALSE_DCBAA_HHDM = 0x42,
    XHCI_M61_RINGS_FALSE_COMMAND_RING_PMM = 0x43,
    XHCI_M61_RINGS_FALSE_COMMAND_RING_HHDM = 0x44,
    XHCI_M61_RINGS_FALSE_EVENT_RING_PMM = 0x45,
    XHCI_M61_RINGS_FALSE_EVENT_RING_HHDM = 0x46,
    XHCI_M61_RINGS_FALSE_ERST_PMM = 0x47,
    XHCI_M61_RINGS_FALSE_ERST_HHDM = 0x48,
    XHCI_M61_RINGS_FALSE_DCBAAP_READBACK = 0x49,
    XHCI_M61_RINGS_PROGRESS_DCBAA_ALLOCATION = 0x4a,
    XHCI_M61_RINGS_PROGRESS_COMMAND_RING_ALLOCATION = 0x4b,
    XHCI_M61_RINGS_PROGRESS_EVENT_RING_ALLOCATION = 0x4c,
    XHCI_M61_RINGS_PROGRESS_ERST_ALLOCATION = 0x4d,
    XHCI_M61_RINGS_PROGRESS_SOFTWARE_INITIALIZATION = 0x4e,
    XHCI_M61_RINGS_PROGRESS_DCBAAP_WRITE = 0x4f,
    XHCI_M61_RINGS_PROGRESS_CRCR_WRITE = 0x50,
    XHCI_M61_RINGS_PROGRESS_CONFIG_WRITE = 0x51,
    XHCI_M61_RINGS_PROGRESS_IMAN_WRITE = 0x52,
    XHCI_M61_RINGS_PROGRESS_ERSTSZ_WRITE = 0x53,
    XHCI_M61_RINGS_PROGRESS_ERSTBA_WRITE = 0x54,
    XHCI_M61_RINGS_PROGRESS_ERDP_WRITE = 0x55,
    XHCI_M61_RINGS_PROGRESS_DCBAAP_READBACK = 0x56,
    XHCI_M61_RINGS_SUCCESS = 0x57,
    XHCI_M61_RINGS_FALSE_SCRATCHPAD_COUNT_BOUND = 0x58,
    XHCI_M61_RINGS_FALSE_SCRATCHPAD_PAGE_SIZE = 0x59,
    XHCI_M61_RINGS_FALSE_SCRATCHPAD_ARRAY_PMM = 0x5a,
    XHCI_M61_RINGS_FALSE_SCRATCHPAD_ARRAY_HHDM = 0x5b,
    XHCI_M61_RINGS_FALSE_SCRATCHPAD_BUFFER_PMM = 0x5c,
    XHCI_M61_RINGS_FALSE_SCRATCHPAD_BUFFER_HHDM = 0x5d,

    XHCI_M61_PROGRESS_CONTROLLER_DISCOVERY = 0x88,
    XHCI_M61_PROGRESS_BAR_VALIDATION = 0x89,
    XHCI_M61_PROGRESS_PCI_ENABLE = 0x8a,
    XHCI_M61_PROGRESS_MMIO_MAP = 0x8b,
    XHCI_M61_PROGRESS_CAPABILITY_PARSE = 0x8c,
    XHCI_M61_PROGRESS_LEGACY_HANDOFF = 0x8d,
    XHCI_M61_PROGRESS_HALT_COMMAND = 0x8e,
    XHCI_M61_PROGRESS_HALT_WAIT = 0x8f,
    XHCI_M61_PROGRESS_RESET_COMMAND = 0xac,
    XHCI_M61_PROGRESS_RESET_WAIT = 0xad,
    XHCI_M61_PROGRESS_CONTROLLER_READY_WAIT = 0xae,
    XHCI_M61_PROGRESS_RINGS_SETUP = 0xaf,
    XHCI_M61_PROGRESS_START_COMMAND = 0xb0,
    XHCI_M61_PROGRESS_START_WAIT = 0xb1,
    XHCI_M61_PROGRESS_PORT_SCAN = 0xb2,

    XHCI_M61_FALSE_INVALID_STATE = 0xb3,
    XHCI_M61_FALSE_NO_CONTROLLER = 0xb4,
    XHCI_M61_FALSE_INVALID_BAR = 0xb5,
    XHCI_M61_FALSE_PCI_ENABLE = 0xb6,
    XHCI_M61_FALSE_MMIO_MAP = 0xb7,
    XHCI_M61_FALSE_CAPABILITIES = 0xb8,
    XHCI_M61_FALSE_LEGACY_HANDOFF = 0xb9,
    XHCI_M61_FALSE_HALT = 0xba,
    XHCI_M61_FALSE_RESET = 0xbb,
    XHCI_M61_FALSE_CONTROLLER_NOT_READY = 0xbc,
    XHCI_M61_FALSE_RINGS_SETUP = 0xbd,
    XHCI_M61_FALSE_START = 0xbe
};

enum xhci_m61_frame_alloc_failure {
    XHCI_M61_FRAME_ALLOC_NONE = 0,
    XHCI_M61_FRAME_ALLOC_PMM,
    XHCI_M61_FRAME_ALLOC_HHDM
};

static uint8_t xhci_m61_failure_reason;
static enum xhci_m61_frame_alloc_failure xhci_m61_frame_alloc_failure;

#define XHCI_M61_BEGIN() \
    do { xhci_m61_failure_reason = 0U; } while (0)
#define XHCI_M61_PROGRESS(code) \
    x86_64_out8((uint16_t)0x80U, (uint8_t)(code))
#define XHCI_M61_FAIL(code) \
    do { \
        xhci_m61_failure_reason = (uint8_t)(code); \
        XHCI_M61_PROGRESS(code); \
        goto out; \
    } while (0)
#define XHCI_M61_FAIL_PRESERVING_REASON(code) \
    do { \
        XHCI_M61_PROGRESS(code); \
        goto out; \
    } while (0)
#define XHCI_M61_RETURN_FALSE(code) \
    do { \
        xhci_m61_failure_reason = (uint8_t)(code); \
        XHCI_M61_PROGRESS(code); \
        return false; \
    } while (0)
#define XHCI_M61_RINGS_ALLOC_RETURN_FALSE(pmm_code, hhdm_code) \
    do { \
        XHCI_M61_RETURN_FALSE( \
            xhci_m61_frame_alloc_failure == XHCI_M61_FRAME_ALLOC_PMM \
                ? (pmm_code) \
                : (hhdm_code)); \
    } while (0)

uint8_t boring_m61_xhci_failure_reason(void) {
    return xhci_m61_failure_reason;
}
#else
#define XHCI_M61_BEGIN() do { } while (0)
#define XHCI_M61_PROGRESS(code) do { } while (0)
#define XHCI_M61_FAIL(code) \
    do { goto out; } while (0)
#define XHCI_M61_FAIL_PRESERVING_REASON(code) \
    do { goto out; } while (0)
#define XHCI_M61_RETURN_FALSE(code) \
    do { return false; } while (0)
#define XHCI_M61_RINGS_ALLOC_RETURN_FALSE(pmm_code, hhdm_code) \
    do { return false; } while (0)
#endif

#define XHCI_CLASS_SERIAL_BUS 0x0cU
#define XHCI_SUBCLASS_USB 0x03U
#define XHCI_PROG_IF 0x30U
#define XHCI_PORT_BASE 0x400U
#define XHCI_PORT_STRIDE 0x10U
#define XHCI_RUNTIME_INTERRUPTER0 0x20U
#define XHCI_WAIT_LIMIT 10000000U
#define XHCI_EXTENDED_CAP_LIMIT 64U
#define XHCI_LEGACY_CAP_ID 1U
#define XHCI_LEGACY_BIOS_OWNED (1U << 16)
#define XHCI_LEGACY_OS_OWNED (1U << 24)
#define XHCI_USBCMD_RUN (1U << 0)
#define XHCI_USBCMD_RESET (1U << 1)
#define XHCI_USBSTS_HALTED (1U << 0)
#define XHCI_USBSTS_NOT_READY (1U << 11)
#define XHCI_TRB_TYPE_LINK 6U
#define XHCI_TRB_TYPE_SHIFT 10U
#define XHCI_TRB_TOGGLE_CYCLE (1U << 1)
#define XHCI_TRB_CYCLE (1U << 0)
#define XHCI_TRB_TYPE_ENABLE_SLOT 9U
#define XHCI_TRB_TYPE_DISABLE_SLOT 10U
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11U
#define XHCI_TRB_TYPE_MASK 0x3fU
#define XHCI_COMMAND_SLOT_SHIFT 24U
#define XHCI_EVENT_WAIT_LIMIT 10000000U
#define XHCI_DMA_32BIT_LIMIT 0x100000000ULL
#define XHCI_PAGESIZE_4K (1U << 0)
#define XHCI_SCRATCHPAD_MAX_SUPPORTED 512U
_Static_assert((XHCI_SCRATCHPAD_MAX_SUPPORTED * sizeof(uint64_t)) <=
               PMM_PAGE_SIZE,
               "xHCI scratchpad array must fit one DMA page");
#define XHCI_PORTSC_CCS (1U << 0)
#define XHCI_PORTSC_PED (1U << 1)
#define XHCI_PORTSC_PR (1U << 4)
#define XHCI_PORTSC_SPEED_SHIFT 10U
#define XHCI_PORTSC_SPEED_MASK 0x0fU
#define XHCI_PORTSC_CHANGE_MASK (0x7fU << 17U)
#define XHCI_PORTSC_PRESERVE_MASK ((1U << 9U) | (3U << 14U) | \
                                   (7U << 25U))

struct xhci_erst_entry {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
};

struct xhci_runtime {
    volatile uint8_t *mmio;
    volatile uint64_t *dcbaa;
    volatile struct xhci_trb *command_ring;
    volatile struct xhci_trb *event_ring;
    uint64_t scratchpad_array_physical;
    uint64_t scratchpad_buffer_physical[XHCI_SCRATCHPAD_MAX_SUPPORTED];
    uint64_t outstanding_command_physical;
    uint16_t scratchpad_count;
    uint16_t command_index;
    uint16_t event_index;
    bool command_cycle;
    bool event_cycle;
    bool command_outstanding;
};

struct xhci_controller {
    struct xhci_state state;
    struct xhci_runtime runtime;
    enum xhci_controller_init_status status;
};

static struct xhci_controller controller_registry[XHCI_MAX_CONTROLLERS];
static uint8_t controller_registry_count;
static bool controller_registry_truncated;

enum xhci_event_expectation {
    XHCI_EXPECT_COMMAND = 0,
    XHCI_EXPECT_CONTROL_FIRST,
    XHCI_EXPECT_CONTROL_STATUS,
    XHCI_EXPECT_CONTROL_NODATA_STATUS
};

struct xhci_dispatch_result {
    uint8_t slot_id;
    uint16_t actual_length;
    bool short_packet;
};

static uint32_t mmio_read32(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)(const volatile void *)(base + offset);
}

static uint64_t mmio_read64(const volatile uint8_t *base, uint32_t offset) {
    const uint64_t low = (uint64_t)mmio_read32(base, offset);
    const uint64_t high = (uint64_t)mmio_read32(base, offset + 4U);
    return low | (high << 32U);
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

static void memory_barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}



static bool legacy_handoff(volatile uint8_t *base,
                           const struct xhci_capabilities *capabilities,
                           bool *complete) {
    uint32_t offset = capabilities->extended_capability_offset;
    uint32_t count;

    *complete = false;
    for (count = 0U; (offset != 0U) &&
                     (count < XHCI_EXTENDED_CAP_LIMIT); ++count) {
        uint32_t header;
        uint32_t next;
        if (offset > XHCI_MMIO_WINDOW_SIZE - 4U) { return false; }
        header = mmio_read32(base, offset);
        if ((header & 0xffU) == XHCI_LEGACY_CAP_ID) {
            uint32_t wait;
            mmio_write32(base, offset, header | XHCI_LEGACY_OS_OWNED);
            for (wait = 0U; wait < XHCI_WAIT_LIMIT; ++wait) {
                header = mmio_read32(base, offset);
                if ((header & XHCI_LEGACY_BIOS_OWNED) == 0U) {
                    *complete = true;
                    return (header & XHCI_LEGACY_OS_OWNED) != 0U;
                }
                x86_64_pause();
            }
            return false;
        }
        next = (header >> 8U) & 0xffU;
        if (next == 0U) { break; }
        if ((next > (XHCI_MMIO_WINDOW_SIZE / 4U)) ||
            (offset > XHCI_MMIO_WINDOW_SIZE - (next * 4U))) {
            return false;
        }
        offset += next * 4U;
    }
    *complete = true;
    return (offset == 0U) || (count < XHCI_EXTENDED_CAP_LIMIT);
}

static bool wait_mask(volatile uint8_t *base, uint32_t offset,
                      uint32_t mask, bool set) {
    uint32_t attempt;
    for (attempt = 0U; attempt < XHCI_WAIT_LIMIT; ++attempt) {
        if (((mmio_read32(base, offset) & mask) != 0U) == set) { return true; }
        x86_64_pause();
    }
    return false;
}

static bool frame_alloc_zero(uint64_t *physical, void **virtual_address) {
    size_t index;
    uint8_t *bytes;
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_NONE;
#endif
    if (!pmm_alloc_frame_in_range(0ULL, XHCI_DMA_32BIT_LIMIT, physical)) {
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
        xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_PMM;
#endif
        return false;
    }
    if (!vmm_pmm_frame_to_hhdm(*physical, virtual_address)) {
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
        xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_HHDM;
#endif
        (void)pmm_free_frame(*physical);
        *physical = 0ULL;
        return false;
    }
    bytes = (uint8_t *)*virtual_address;
    for (index = 0U; index < PMM_PAGE_SIZE; ++index) { bytes[index] = 0U; }
    return true;
}

static void frame_release(uint64_t physical) {
    if (physical != 0ULL) { (void)pmm_free_frame(physical); }
}

static void scratchpads_release(struct xhci_controller *controller) {
    uint16_t index;
    if ((controller->runtime.dcbaa != NULL) &&
        (controller->runtime.scratchpad_array_physical != 0ULL)) {
        controller->runtime.dcbaa[0] = 0ULL;
        memory_barrier();
    }
    for (index = 0U; index < controller->runtime.scratchpad_count; ++index) {
        frame_release(controller->runtime.scratchpad_buffer_physical[index]);
        controller->runtime.scratchpad_buffer_physical[index] = 0ULL;
    }
    frame_release(controller->runtime.scratchpad_array_physical);
    controller->runtime.scratchpad_array_physical = 0ULL;
    controller->runtime.scratchpad_count = 0U;
}

static bool scratchpads_initialize(struct xhci_controller *controller, 
    volatile uint8_t *base, uint32_t operational,
    const struct xhci_capabilities *capabilities) {
    void *array_virtual = NULL;
    volatile uint64_t *array;
    uint16_t index;

    if (capabilities->scratchpad_count == 0U) { return true; }
    if (capabilities->scratchpad_count > XHCI_SCRATCHPAD_MAX_SUPPORTED) {
        XHCI_M61_RETURN_FALSE(XHCI_M61_RINGS_FALSE_SCRATCHPAD_COUNT_BOUND);
    }
    if ((mmio_read32(base, operational + 0x08U) & XHCI_PAGESIZE_4K) == 0U) {
        XHCI_M61_RETURN_FALSE(XHCI_M61_RINGS_FALSE_SCRATCHPAD_PAGE_SIZE);
    }
    controller->runtime.scratchpad_count = capabilities->scratchpad_count;
    if (!frame_alloc_zero(&controller->runtime.scratchpad_array_physical,
                          &array_virtual)) {
        XHCI_M61_RINGS_ALLOC_RETURN_FALSE(
            XHCI_M61_RINGS_FALSE_SCRATCHPAD_ARRAY_PMM,
            XHCI_M61_RINGS_FALSE_SCRATCHPAD_ARRAY_HHDM);
    }
    array = (volatile uint64_t *)array_virtual;
    for (index = 0U; index < controller->runtime.scratchpad_count; ++index) {
        void *buffer_virtual = NULL;
        if (!frame_alloc_zero(&controller->runtime.scratchpad_buffer_physical[index],
                              &buffer_virtual)) {
            XHCI_M61_RINGS_ALLOC_RETURN_FALSE(
                XHCI_M61_RINGS_FALSE_SCRATCHPAD_BUFFER_PMM,
                XHCI_M61_RINGS_FALSE_SCRATCHPAD_BUFFER_HHDM);
        }
        (void)buffer_virtual;
        array[index] = controller->runtime.scratchpad_buffer_physical[index];
    }
    memory_barrier();
    controller->runtime.dcbaa[0] = controller->runtime.scratchpad_array_physical;
    memory_barrier();
    return true;
}

static void rings_release(struct xhci_controller *controller, struct xhci_state *state) {
    if (state == NULL) { return; }
    scratchpads_release(controller);
    frame_release(state->erst_physical);
    state->erst_physical = 0ULL;
    frame_release(state->event_ring_physical);
    state->event_ring_physical = 0ULL;
    frame_release(state->command_ring_physical);
    state->command_ring_physical = 0ULL;
    frame_release(state->dcbaa_physical);
    state->dcbaa_physical = 0ULL;
    controller->runtime.dcbaa = NULL;
    controller->runtime.command_ring = NULL;
    controller->runtime.event_ring = NULL;
}

static bool rings_initialize(struct xhci_controller *controller, volatile uint8_t *base,
                             const struct xhci_capabilities *capabilities,
                             struct xhci_state *state) {
    void *dcbaa_virtual = NULL;
    void *command_virtual = NULL;
    void *event_virtual = NULL;
    void *erst_virtual = NULL;
    struct xhci_trb *command;
    struct xhci_erst_entry *erst;
    const uint32_t operational = capabilities->capability_length;
    const uint32_t interrupter = capabilities->runtime_offset +
                                 XHCI_RUNTIME_INTERRUPTER0;

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_DCBAA_ALLOCATION);
    if (!frame_alloc_zero(&state->dcbaa_physical, &dcbaa_virtual)) {
        XHCI_M61_RINGS_ALLOC_RETURN_FALSE(XHCI_M61_RINGS_FALSE_DCBAA_PMM,
                                          XHCI_M61_RINGS_FALSE_DCBAA_HHDM);
    }
    controller->runtime.dcbaa = (volatile uint64_t *)dcbaa_virtual;
    if (!scratchpads_initialize(controller, base, operational, capabilities)) {
        return false;
    }
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_COMMAND_RING_ALLOCATION);
    if (!frame_alloc_zero(&state->command_ring_physical, &command_virtual)) {
        XHCI_M61_RINGS_ALLOC_RETURN_FALSE(
            XHCI_M61_RINGS_FALSE_COMMAND_RING_PMM,
            XHCI_M61_RINGS_FALSE_COMMAND_RING_HHDM);
    }
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_EVENT_RING_ALLOCATION);
    if (!frame_alloc_zero(&state->event_ring_physical, &event_virtual)) {
        XHCI_M61_RINGS_ALLOC_RETURN_FALSE(
            XHCI_M61_RINGS_FALSE_EVENT_RING_PMM,
            XHCI_M61_RINGS_FALSE_EVENT_RING_HHDM);
    }
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_ERST_ALLOCATION);
    if (!frame_alloc_zero(&state->erst_physical, &erst_virtual)) {
        XHCI_M61_RINGS_ALLOC_RETURN_FALSE(XHCI_M61_RINGS_FALSE_ERST_PMM,
                                          XHCI_M61_RINGS_FALSE_ERST_HHDM);
    }
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_SOFTWARE_INITIALIZATION);
#else
    if (!frame_alloc_zero(&state->dcbaa_physical, &dcbaa_virtual)) {
        return false;
    }
    controller->runtime.dcbaa = (volatile uint64_t *)dcbaa_virtual;
    if (!scratchpads_initialize(controller, base, operational, capabilities)) {
        return false;
    }
    if (!frame_alloc_zero(&state->command_ring_physical, &command_virtual) ||
        !frame_alloc_zero(&state->event_ring_physical, &event_virtual) ||
        !frame_alloc_zero(&state->erst_physical, &erst_virtual)) {
        return false;
    }
#endif
    (void)dcbaa_virtual;
    (void)event_virtual;
    command = (struct xhci_trb *)command_virtual;
    command[XHCI_COMMAND_RING_USABLE].parameter =
        state->command_ring_physical;
    command[XHCI_COMMAND_RING_USABLE].status = 0U;
    command[XHCI_COMMAND_RING_USABLE].control =
        ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
    erst = (struct xhci_erst_entry *)erst_virtual;
    erst->ring_base = state->event_ring_physical;
    erst->ring_size = XHCI_EVENT_RING_TRBS;
    erst->reserved = 0U;
    memory_barrier();

    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_DCBAAP_WRITE);
    mmio_write64(base, operational + 0x30U, state->dcbaa_physical);
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_CRCR_WRITE);
    mmio_write64(base, operational + 0x18U,
                 state->command_ring_physical | XHCI_TRB_CYCLE);
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_CONFIG_WRITE);
    mmio_write32(base, operational + 0x38U,
                 (uint32_t)capabilities->max_slots);
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_IMAN_WRITE);
    mmio_write32(base, interrupter + 0x00U, 0U);
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_ERSTSZ_WRITE);
    mmio_write32(base, interrupter + 0x08U, 1U);
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_ERSTBA_WRITE);
    mmio_write64(base, interrupter + 0x10U, state->erst_physical);
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_ERDP_WRITE);
    mmio_write64(base, interrupter + 0x18U, state->event_ring_physical);
    memory_barrier();
    controller->runtime.mmio = base;
    controller->runtime.command_ring =
        (volatile struct xhci_trb *)command_virtual;
    controller->runtime.event_ring = (volatile struct xhci_trb *)event_virtual;
    controller->runtime.command_index = 0U;
    controller->runtime.event_index = 0U;
    controller->runtime.command_cycle = true;
    controller->runtime.event_cycle = true;
    controller->runtime.command_outstanding = false;
    controller->runtime.outstanding_command_physical = 0ULL;
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_DCBAAP_READBACK);
    if (mmio_read64(base, operational + 0x30U) != state->dcbaa_physical) {
        XHCI_M61_RETURN_FALSE(XHCI_M61_RINGS_FALSE_DCBAAP_READBACK);
    }
    XHCI_M61_PROGRESS(XHCI_M61_RINGS_SUCCESS);
    return true;
#else
    return (mmio_read64(base, operational + 0x30U) ==
            state->dcbaa_physical);
#endif
}

static void state_clear(struct xhci_state *state) {
    uint8_t *bytes = (uint8_t *)state;
    size_t index;
    for (index = 0U; index < sizeof(*state); ++index) { bytes[index] = 0U; }
}

static void runtime_clear(struct xhci_controller *controller) {
    uint8_t *bytes = (uint8_t *)&controller->runtime;
    size_t index;
    for (index = 0U; index < sizeof(controller->runtime); ++index) {
        bytes[index] = 0U;
    }
}

static uint32_t port_offset(struct xhci_controller *controller, uint8_t root_port_id) {
    return (uint32_t)controller->state.capabilities.capability_length +
           XHCI_PORT_BASE + (((uint32_t)root_port_id - 1U) * XHCI_PORT_STRIDE);
}

static bool command_submit(struct xhci_controller *controller, uint64_t parameter, uint32_t status,
                           uint32_t control, uint64_t *command_physical) {
    volatile struct xhci_trb *trb;
    uint16_t index;
    if ((controller->runtime.mmio == NULL) ||
        (controller->runtime.command_ring == NULL) || (command_physical == NULL) ||
        controller->runtime.command_outstanding ||
        (controller->runtime.command_index >= XHCI_COMMAND_RING_USABLE)) {
        return false;
    }
    index = controller->runtime.command_index;
    trb = &controller->runtime.command_ring[index];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control |
                   (controller->runtime.command_cycle ? XHCI_TRB_CYCLE : 0U);
    *command_physical = controller->state.command_ring_physical +
                        ((uint64_t)index * XHCI_TRB_SIZE);
    ++controller->runtime.command_index;
    if (controller->runtime.command_index == XHCI_COMMAND_RING_USABLE) {
        volatile struct xhci_trb *link =
            &controller->runtime.command_ring[XHCI_COMMAND_RING_USABLE];
        link->control = ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        XHCI_TRB_TOGGLE_CYCLE |
                        (controller->runtime.command_cycle ? XHCI_TRB_CYCLE : 0U);
        controller->runtime.command_index = 0U;
        controller->runtime.command_cycle = !controller->runtime.command_cycle;
    }
    controller->runtime.command_outstanding = true;
    controller->runtime.outstanding_command_physical = *command_physical;
    memory_barrier();
    mmio_write32(controller->runtime.mmio,
                 controller->state.capabilities.doorbell_offset, 0U);
    return true;
}

static bool event_take(struct xhci_controller *controller, struct xhci_trb *event, bool *available) {
    const volatile struct xhci_trb *source;
    uint32_t control;
    uint16_t event_index;
    uint16_t next_index;
    bool event_cycle;
    bool next_cycle;
    const uint32_t interrupter = controller->state.capabilities.runtime_offset +
                                 XHCI_RUNTIME_INTERRUPTER0;
    if ((event == NULL) || (available == NULL) ||
        (controller->runtime.mmio == NULL) || (controller->runtime.event_ring == NULL) ||
        !xhci_event_dequeue_position(&controller->state, &event_index,
                                     &event_cycle)) {
        return false;
    }
    source = &controller->runtime.event_ring[event_index];
    control = source->control;
    if (((control & XHCI_TRB_CYCLE) != 0U) != event_cycle) {
        *available = false;
        return true;
    }
    event->parameter = source->parameter;
    event->status = source->status;
    event->control = control;
    if (!xhci_event_dequeue_advance(&controller->state, event_index, event_cycle,
                                    &next_index, &next_cycle)) {
        return false;
    }
    controller->runtime.event_index = next_index;
    controller->runtime.event_cycle = next_cycle;
    memory_barrier();
    mmio_write64(controller->runtime.mmio, interrupter + 0x18U,
                 (controller->state.event_ring_physical +
                  ((uint64_t)next_index * XHCI_TRB_SIZE)) |
                 (1ULL << 3U));
    *available = true;
    return true;
}

static bool consume_port_event(struct xhci_controller *controller, const struct xhci_trb *event) {
    return xhci_consume_port_status_event(&controller->state, event);
}

static bool event_dispatch_wait(struct xhci_controller *controller, enum xhci_event_expectation expectation,
                                uint64_t command_physical,
                                struct xhci_addressed_device *device,
                                struct xhci_dispatch_result *result) {
    uint32_t attempt;
    if (result == NULL) { return false; }
    for (attempt = 0U; attempt < XHCI_EVENT_WAIT_LIMIT; ++attempt) {
        struct xhci_trb event;
        bool available;
        uint8_t type;
        if (!event_take(controller, &event, &available)) { return false; }
        if (!available) {
            x86_64_pause();
            continue;
        }
        type = (uint8_t)((event.control >> XHCI_TRB_TYPE_SHIFT) &
                         XHCI_TRB_TYPE_MASK);
        if (type == XHCI_TRB_TYPE_PORT_STATUS_EVENT) {
            if (!consume_port_event(controller, &event)) { return false; }
            continue;
        }
        if (expectation == XHCI_EXPECT_COMMAND) {
            uint8_t completed_slot;
            if (!controller->runtime.command_outstanding ||
                (controller->runtime.outstanding_command_physical !=
                 command_physical) ||
                (type != XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT) ||
                !xhci_validate_command_completion(
                    &event, command_physical,
                    controller->state.capabilities.max_slots, &completed_slot)) {
                return false;
            }
            controller->runtime.command_outstanding = false;
            controller->runtime.outstanding_command_physical = 0ULL;
            result->slot_id = completed_slot;
            result->actual_length = 0U;
            result->short_packet = false;
            if (controller->state.command_completions != UINT32_MAX) {
                ++controller->state.command_completions;
            }
            return true;
        }
        if ((device == NULL) || !device->control_outstanding ||
            (type != XHCI_TRB_TYPE_TRANSFER_EVENT)) {
            return false;
        }
        if (expectation == XHCI_EXPECT_CONTROL_NODATA_STATUS) {
            if (!xhci_validate_no_data_control_event(
                    &event, device->ep0_ring_physical, device->slot_id,
                    device->expected_status_trb_physical)) {
                return false;
            }
            result->actual_length = 0U;
            result->short_packet = false;
        } else if (!xhci_validate_control_transfer_event(
                       &event, device->ep0_ring_physical, device->slot_id,
                       device->expected_data_trb_physical,
                       device->expected_status_trb_physical,
                       device->outstanding_length,
                       expectation == XHCI_EXPECT_CONTROL_STATUS,
                       &result->actual_length, &result->short_packet)) {
            return false;
        }
        result->slot_id = device->slot_id;
        if (device->transfer_events != UINT32_MAX) { ++device->transfer_events; }
        return true;
    }
    return false;
}

static bool command_wait(struct xhci_controller *controller, uint64_t command_physical, uint8_t *slot_id) {
    struct xhci_dispatch_result result;
    bool success;
    if ((slot_id == NULL) || !controller->runtime.command_outstanding ||
        (controller->runtime.outstanding_command_physical != command_physical)) {
        return false;
    }
    success = event_dispatch_wait(controller, XHCI_EXPECT_COMMAND, command_physical,
                                  NULL, &result);
    if (!success) {
        controller->runtime.command_outstanding = false;
        controller->runtime.outstanding_command_physical = 0ULL;
        return false;
    }
    *slot_id = result.slot_id;
    return true;
}

static bool command_enable_slot(struct xhci_controller *controller, uint8_t *slot_id) {
    uint64_t command_physical;
    return command_submit(controller, 0ULL, 0U,
                          (uint32_t)XHCI_TRB_TYPE_ENABLE_SLOT <<
                          XHCI_TRB_TYPE_SHIFT,
                          &command_physical) &&
           command_wait(controller, command_physical, slot_id);
}

static bool command_disable_slot(struct xhci_controller *controller, uint8_t slot_id) {
    uint64_t command_physical;
    uint8_t completed_slot;
    if ((slot_id == 0U) || (slot_id > controller->state.capabilities.max_slots)) {
        return false;
    }
    return command_submit(controller, 0ULL, 0U,
                          ((uint32_t)XHCI_TRB_TYPE_DISABLE_SLOT <<
                           XHCI_TRB_TYPE_SHIFT) |
                          ((uint32_t)slot_id << XHCI_COMMAND_SLOT_SHIFT),
                          &command_physical) &&
           command_wait(controller, command_physical, &completed_slot) &&
           (completed_slot == slot_id);
}

static bool command_address_device(struct xhci_controller *controller, uint8_t slot_id,
                                   uint64_t input_context_physical) {
    uint64_t command_physical;
    uint8_t completed_slot;
    if ((slot_id == 0U) || (slot_id > controller->state.capabilities.max_slots) ||
        ((input_context_physical & 0x3fULL) != 0ULL)) {
        return false;
    }
    return command_submit(controller, input_context_physical, 0U,
                          ((uint32_t)XHCI_TRB_TYPE_ADDRESS_DEVICE <<
                           XHCI_TRB_TYPE_SHIFT) |
                          ((uint32_t)slot_id << XHCI_COMMAND_SLOT_SHIFT),
                          &command_physical) &&
           command_wait(controller, command_physical, &completed_slot) &&
           (completed_slot == slot_id);
}

static bool command_evaluate_ep0(struct xhci_controller *controller, struct xhci_addressed_device *device,
                                 uint16_t max_packet) {
    void *input_virtual = NULL;
    uint64_t command_physical;
    uint8_t completed_slot;
    if ((device == NULL) || !device->addressed || device->control_outstanding ||
        controller->runtime.command_outstanding ||
        !vmm_pmm_frame_to_hhdm(device->input_context_physical, &input_virtual) ||
        !xhci_build_evaluate_ep0_context(
            input_virtual, (uint32_t)PMM_PAGE_SIZE,
            controller->state.capabilities.context_64_bytes,
            controller->state.capabilities.max_slots, device->slot_id, max_packet,
            device->ep0_ring_physical)) {
        return false;
    }
    memory_barrier();
    if (!command_submit(controller, device->input_context_physical, 0U,
                        ((uint32_t)XHCI_TRB_TYPE_EVALUATE_CONTEXT <<
                         XHCI_TRB_TYPE_SHIFT) |
                        ((uint32_t)device->slot_id << XHCI_COMMAND_SLOT_SHIFT),
                        &command_physical) ||
        !command_wait(controller, command_physical, &completed_slot) ||
        (completed_slot != device->slot_id)) {
        return false;
    }
    device->ep0_max_packet = max_packet;
    if (device->evaluate_context_completions != UINT32_MAX) {
        ++device->evaluate_context_completions;
    }
    return true;
}

static bool root_port_reset(struct xhci_controller *controller, uint8_t root_port_id, uint8_t *speed) {
    uint32_t current;
    uint32_t attempt;
    const uint32_t offset = port_offset(controller, root_port_id);
    if ((speed == NULL) || (root_port_id == 0U) ||
        (root_port_id > controller->state.capabilities.max_ports)) {
        return false;
    }
    current = mmio_read32(controller->runtime.mmio, offset);
    if ((current & XHCI_PORTSC_CCS) == 0U) { return false; }
    if ((current & XHCI_PORTSC_PED) == 0U) {
        mmio_write32(controller->runtime.mmio, offset,
                     (current & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PR);
        for (attempt = 0U; attempt < XHCI_WAIT_LIMIT; ++attempt) {
            current = mmio_read32(controller->runtime.mmio, offset);
            if (((current & XHCI_PORTSC_PR) == 0U) &&
                ((current & XHCI_PORTSC_PED) != 0U)) {
                break;
            }
            x86_64_pause();
        }
        if (attempt == XHCI_WAIT_LIMIT) { return false; }
    }
    current = mmio_read32(controller->runtime.mmio, offset);
    if (((current & XHCI_PORTSC_CCS) == 0U) ||
        ((current & XHCI_PORTSC_PED) == 0U)) {
        return false;
    }
    *speed = (uint8_t)((current >> XHCI_PORTSC_SPEED_SHIFT) &
                       XHCI_PORTSC_SPEED_MASK);
    if ((*speed == 0U) || (*speed > 5U)) { return false; }
    mmio_write32(controller->runtime.mmio, offset,
                 (current & XHCI_PORTSC_PRESERVE_MASK) |
                 (current & XHCI_PORTSC_CHANGE_MASK));
    return true;
}

static void device_frames_release(struct xhci_addressed_device *device) {
    uint8_t *bytes = (uint8_t *)device;
    size_t index;
    for (index = 0U; index < XHCI_MAX_HID_ENDPOINTS; ++index) {
        frame_release(device->hid_ring_physical[index]);
    }
    frame_release(device->descriptor_buffer_physical);
    frame_release(device->ep0_ring_physical);
    frame_release(device->device_context_physical);
    frame_release(device->input_context_physical);
    for (index = 0U; index < sizeof(*device); ++index) { bytes[index] = 0U; }
}

static bool address_root_port(struct xhci_controller *controller, uint8_t root_port_id,
                              struct xhci_addressed_device *device) {
    void *input_virtual = NULL;
    void *device_virtual = NULL;
    void *ep0_virtual = NULL;
    struct xhci_trb *ep0_ring;
    uint8_t slot_id = 0U;
    uint8_t speed;
    uint16_t initial_max_packet;
    bool slot_enabled = false;

    if ((device == NULL) || !root_port_reset(controller, root_port_id, &speed) ||
        !xhci_ep0_max_packet(speed, &initial_max_packet) ||
        !command_enable_slot(controller, &slot_id)) {
        return false;
    }
    slot_enabled = true;
    if ((slot_id == 0U) || (slot_id > controller->state.capabilities.max_slots) ||
        !frame_alloc_zero(&device->input_context_physical, &input_virtual) ||
        !frame_alloc_zero(&device->device_context_physical, &device_virtual) ||
        !frame_alloc_zero(&device->ep0_ring_physical, &ep0_virtual)) {
        goto fail;
    }
    ep0_ring = (struct xhci_trb *)ep0_virtual;
    ep0_ring[XHCI_EP0_RING_USABLE].parameter = device->ep0_ring_physical;
    ep0_ring[XHCI_EP0_RING_USABLE].status = 0U;
    ep0_ring[XHCI_EP0_RING_USABLE].control =
        ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
    if (!xhci_build_address_input_context(
            input_virtual, (uint32_t)PMM_PAGE_SIZE,
            controller->state.capabilities.context_64_bytes,
            controller->state.capabilities.max_slots, slot_id,
            controller->state.capabilities.max_ports, root_port_id, speed,
            device->ep0_ring_physical)) {
        goto fail;
    }
    controller->runtime.dcbaa[slot_id] = device->device_context_physical;
    memory_barrier();
    if (!command_address_device(controller, slot_id, device->input_context_physical)) {
        controller->runtime.dcbaa[slot_id] = 0ULL;
        memory_barrier();
        goto fail;
    }
    (void)device_virtual;
    device->root_port_id = root_port_id;
    device->slot_id = slot_id;
    device->speed = speed;
    device->controller_index = controller->state.controller_index;
    device->ep0_producer_index = 0U;
    device->ep0_producer_cycle = true;
    device->ep0_max_packet = initial_max_packet;
    device->addressed = true;
    return true;

fail:
    if (slot_enabled) { (void)command_disable_slot(controller, slot_id); }
    device_frames_release(device);
    return false;
}

static bool ep0_submit_get_descriptor(struct xhci_controller *controller, struct xhci_addressed_device *device,
                                      uint8_t descriptor_type,
                                      uint8_t descriptor_index,
                                      uint16_t length,
                                      uint16_t *actual_length) {
    void *ep0_virtual = NULL;
    void *buffer_virtual = NULL;
    volatile struct xhci_trb *ring;
    uint8_t *buffer;
    struct xhci_control_td td;
    struct xhci_dispatch_result first;
    struct xhci_dispatch_result status;
    uint16_t start_index;
    uint16_t actual;
    uint32_t doorbell;
    size_t index;

    if ((device == NULL) || (actual_length == NULL) || !device->addressed ||
        device->control_outstanding || controller->runtime.command_outstanding ||
        (device->descriptor_buffer_physical == 0ULL) ||
        (length == 0U) || (length > XHCI_DESCRIPTOR_BUFFER_BYTES) ||
        (device->slot_id == 0U) ||
        (controller->state.capabilities.doorbell_offset >
         XHCI_MMIO_WINDOW_SIZE - 4U) ||
        ((uint32_t)device->slot_id >
         (XHCI_MMIO_WINDOW_SIZE - controller->state.capabilities.doorbell_offset -
          4U) / 4U) ||
        !vmm_pmm_frame_to_hhdm(device->ep0_ring_physical, &ep0_virtual) ||
        !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                               &buffer_virtual) ||
        !xhci_build_get_descriptor_control_td(
            &td, device->ep0_ring_physical, device->ep0_producer_index,
            device->ep0_producer_cycle, device->descriptor_buffer_physical,
            descriptor_type, descriptor_index, length)) {
        return false;
    }
    ring = (volatile struct xhci_trb *)ep0_virtual;
    buffer = (uint8_t *)buffer_virtual;
    for (index = 0U; index < (size_t)length; ++index) { buffer[index] = 0U; }

    start_index = device->ep0_producer_index;
    ring[start_index] = td.setup;
    ring[(uint16_t)(start_index + 1U)] = td.data;
    ring[(uint16_t)(start_index + 2U)] = td.status;
    if (start_index == XHCI_EP0_RING_USABLE - 3U) {
        ring[XHCI_EP0_RING_USABLE].parameter = device->ep0_ring_physical;
        ring[XHCI_EP0_RING_USABLE].status = 0U;
        ring[XHCI_EP0_RING_USABLE].control =
            ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            (device->ep0_producer_cycle ? XHCI_TRB_CYCLE : 0U);
    }
    device->expected_data_trb_physical = td.data_physical;
    device->expected_status_trb_physical = td.status_physical;
    device->outstanding_length = length;
    device->control_outstanding = true;
    device->ep0_producer_index = td.next_producer_index;
    device->ep0_producer_cycle = td.next_producer_cycle;
    memory_barrier();
    doorbell = controller->state.capabilities.doorbell_offset +
               ((uint32_t)device->slot_id * 4U);
    mmio_write32(controller->runtime.mmio, doorbell, 1U);

    if (!event_dispatch_wait(controller, XHCI_EXPECT_CONTROL_FIRST, 0ULL, device, &first)) {
        device->control_outstanding = false;
        return false;
    }
    actual = first.actual_length;
    if (first.short_packet) {
        if (device->short_packets != UINT32_MAX) { ++device->short_packets; }
        if (!event_dispatch_wait(controller, XHCI_EXPECT_CONTROL_STATUS, 0ULL, device,
                                 &status)) {
            device->control_outstanding = false;
            return false;
        }
    }
    device->control_outstanding = false;
    device->expected_data_trb_physical = 0ULL;
    device->expected_status_trb_physical = 0ULL;
    device->outstanding_length = 0U;
    if (device->descriptor_bytes > UINT32_MAX - (uint32_t)actual) {
        return false;
    }
    device->descriptor_bytes += (uint32_t)actual;
    *actual_length = actual;
    return true;
}

static bool descriptor_buffer_bytes(struct xhci_addressed_device *device,
                                    uint8_t **bytes) {
    void *virtual_address = NULL;
    if ((device == NULL) || (bytes == NULL) ||
        (device->descriptor_buffer_physical == 0ULL) ||
        !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                               &virtual_address)) {
        return false;
    }
    *bytes = (uint8_t *)virtual_address;
    return true;
}

static bool ep0_submit_set_configuration(struct xhci_controller *controller, 
    struct xhci_addressed_device *device, uint8_t configuration_value) {
    void *ep0_virtual = NULL;
    volatile struct xhci_trb *ring;
    struct xhci_control_td td;
    struct xhci_dispatch_result result;
    uint16_t start_index;
    uint32_t doorbell;
    if ((device == NULL) || !device->addressed || !device->descriptors_ready ||
        device->control_outstanding || controller->runtime.command_outstanding ||
        (configuration_value == 0U) || (device->slot_id == 0U) ||
        (controller->state.capabilities.doorbell_offset > XHCI_MMIO_WINDOW_SIZE - 4U) ||
        ((uint32_t)device->slot_id >
         (XHCI_MMIO_WINDOW_SIZE - controller->state.capabilities.doorbell_offset - 4U) / 4U) ||
        !vmm_pmm_frame_to_hhdm(device->ep0_ring_physical, &ep0_virtual) ||
        !xhci_build_set_configuration_control_td(
            &td, device->ep0_ring_physical, device->ep0_producer_index,
            device->ep0_producer_cycle, configuration_value)) {
        return false;
    }
    ring = (volatile struct xhci_trb *)ep0_virtual;
    start_index = device->ep0_producer_index;
    ring[start_index] = td.setup;
    ring[(uint16_t)(start_index + 1U)] = td.status;
    if (start_index == XHCI_EP0_RING_USABLE - 2U) {
        ring[XHCI_EP0_RING_USABLE].parameter = device->ep0_ring_physical;
        ring[XHCI_EP0_RING_USABLE].status = 0U;
        ring[XHCI_EP0_RING_USABLE].control =
            ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            (device->ep0_producer_cycle ? XHCI_TRB_CYCLE : 0U);
    }
    device->expected_data_trb_physical = 0ULL;
    device->expected_status_trb_physical = td.status_physical;
    device->outstanding_length = 0U;
    device->control_outstanding = true;
    device->ep0_producer_index = td.next_producer_index;
    device->ep0_producer_cycle = td.next_producer_cycle;
    memory_barrier();
    doorbell = controller->state.capabilities.doorbell_offset +
               ((uint32_t)device->slot_id * 4U);
    mmio_write32(controller->runtime.mmio, doorbell, 1U);
    if (!event_dispatch_wait(controller, XHCI_EXPECT_CONTROL_NODATA_STATUS, 0ULL,
                             device, &result)) {
        device->control_outstanding = false;
        device->expected_status_trb_physical = 0ULL;
        return false;
    }
    device->control_outstanding = false;
    device->expected_status_trb_physical = 0ULL;
    if (device->set_configuration_completions == UINT32_MAX) { return false; }
    ++device->set_configuration_completions;
    return true;
}

static bool ep0_submit_hid_set_protocol(struct xhci_controller *controller, 
    struct xhci_addressed_device *device, uint8_t interface_number) {
    void *ep0_virtual = NULL;
    volatile struct xhci_trb *ring;
    struct xhci_control_td td;
    struct xhci_dispatch_result result;
    uint16_t start_index;
    uint32_t doorbell;
    if ((device == NULL) || !device->addressed || !device->descriptors_ready ||
        device->control_outstanding || controller->runtime.command_outstanding ||
        (device->slot_id == 0U) ||
        (device->set_protocol_completions == UINT32_MAX) ||
        (controller->state.capabilities.doorbell_offset > XHCI_MMIO_WINDOW_SIZE - 4U) ||
        ((uint32_t)device->slot_id >
         (XHCI_MMIO_WINDOW_SIZE - controller->state.capabilities.doorbell_offset - 4U) / 4U) ||
        !vmm_pmm_frame_to_hhdm(device->ep0_ring_physical, &ep0_virtual) ||
        !xhci_build_hid_set_protocol_control_td(
            &td, device->ep0_ring_physical, device->ep0_producer_index,
            device->ep0_producer_cycle, interface_number)) {
        return false;
    }
    ring = (volatile struct xhci_trb *)ep0_virtual;
    start_index = device->ep0_producer_index;
    ring[start_index] = td.setup;
    ring[(uint16_t)(start_index + 1U)] = td.status;
    if (start_index == XHCI_EP0_RING_USABLE - 2U) {
        ring[XHCI_EP0_RING_USABLE].parameter = device->ep0_ring_physical;
        ring[XHCI_EP0_RING_USABLE].status = 0U;
        ring[XHCI_EP0_RING_USABLE].control =
            ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            (device->ep0_producer_cycle ? XHCI_TRB_CYCLE : 0U);
    }
    device->expected_data_trb_physical = 0ULL;
    device->expected_status_trb_physical = td.status_physical;
    device->outstanding_length = 0U;
    device->control_outstanding = true;
    device->ep0_producer_index = td.next_producer_index;
    device->ep0_producer_cycle = td.next_producer_cycle;
    memory_barrier();
    doorbell = controller->state.capabilities.doorbell_offset +
               ((uint32_t)device->slot_id * 4U);
    mmio_write32(controller->runtime.mmio, doorbell, 1U);
    if (!event_dispatch_wait(controller, XHCI_EXPECT_CONTROL_NODATA_STATUS, 0ULL,
                             device, &result)) {
        device->control_outstanding = false;
        device->expected_status_trb_physical = 0ULL;
        return false;
    }
    device->control_outstanding = false;
    device->expected_status_trb_physical = 0ULL;
    ++device->set_protocol_completions;
    return true;
}

static bool configure_hid_boot_protocols(struct xhci_controller *controller, 
    struct xhci_addressed_device *device,
    const struct xhci_hid_configuration *configuration) {
    uint8_t index;
    if ((device == NULL) || (configuration == NULL)) { return false; }
    for (index = 0U; index < configuration->endpoint_count; ++index) {
        const struct xhci_hid_endpoint_descriptor *endpoint =
            &configuration->endpoints[index];
        uint8_t previous;
        bool already_selected = false;
        if ((endpoint->report_format != XHCI_HID_REPORT_BOOT_KEYBOARD) &&
            (endpoint->report_format != XHCI_HID_REPORT_BOOT_MOUSE)) {
            continue;
        }
        for (previous = 0U; previous < index; ++previous) {
            const struct xhci_hid_endpoint_descriptor *candidate =
                &configuration->endpoints[previous];
            if (((candidate->report_format == XHCI_HID_REPORT_BOOT_KEYBOARD) ||
                 (candidate->report_format == XHCI_HID_REPORT_BOOT_MOUSE)) &&
                (candidate->interface_number == endpoint->interface_number)) {
                already_selected = true;
                break;
            }
        }
        if (!already_selected && !ep0_submit_hid_set_protocol(controller, 
                device, endpoint->interface_number)) {
            return false;
        }
    }
    return true;
}

static bool command_configure_hid(struct xhci_controller *controller, struct xhci_addressed_device *device) {
    struct xhci_trb command;
    uint64_t command_physical;
    uint8_t completed_slot;
    if ((device == NULL) ||
        !xhci_build_configure_endpoint_command(
            &command, device->input_context_physical,
            controller->state.capabilities.max_slots, device->slot_id) ||
        !command_submit(controller, command.parameter, command.status, command.control,
                        &command_physical) ||
        !command_wait(controller, command_physical, &completed_slot) ||
        (completed_slot != device->slot_id)) {
        return false;
    }
    return true;
}

static bool configure_hid_device(struct xhci_controller *controller, struct xhci_addressed_device *device) {
    struct xhci_hid_configuration parsed_configuration;
    struct xhci_hid_configuration configuration;
    uint64_t rings[XHCI_MAX_HID_ENDPOINTS] = {0ULL};
    void *input_virtual = NULL;
    uint8_t *descriptor_bytes = NULL;
    uint8_t index;
    bool success = false;
    if ((device == NULL) || !device->addressed || !device->descriptors_ready ||
        device->device_configured || device->hid_endpoint_ready ||
        device->control_outstanding || controller->runtime.command_outstanding ||
        !descriptor_buffer_bytes(device, &descriptor_bytes) ||
        !xhci_parse_hid_configuration(
            descriptor_bytes, device->descriptors.configuration_length,
            device->speed, &parsed_configuration) ||
        !xhci_select_supported_hid_configuration(
            &parsed_configuration, device->descriptors.vendor_id,
            device->descriptors.product_id, &configuration)) {
        return false;
    }
    if (configuration.endpoint_count == 0U) { return true; }
    if (!ep0_submit_set_configuration(controller, device, configuration.configuration_value) ||
        !configure_hid_boot_protocols(controller, device, &configuration)) {
        return false;
    }
    for (index = 0U; index < configuration.endpoint_count; ++index) {
        void *ring_virtual = NULL;
        struct xhci_trb *ring;
        if (!frame_alloc_zero(&rings[index], &ring_virtual)) { goto out; }
        ring = (struct xhci_trb *)ring_virtual;
        ring[XHCI_INTERRUPT_RING_USABLE].parameter = rings[index];
        ring[XHCI_INTERRUPT_RING_USABLE].status = 0U;
        ring[XHCI_INTERRUPT_RING_USABLE].control =
            ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
    }
    if (!vmm_pmm_frame_to_hhdm(device->input_context_physical, &input_virtual) ||
        !xhci_build_configure_hid_context(
            input_virtual, (uint32_t)PMM_PAGE_SIZE,
            controller->state.capabilities.context_64_bytes,
            controller->state.capabilities.max_slots, device->slot_id,
            controller->state.capabilities.max_ports, device->root_port_id,
            device->speed, &configuration, rings)) {
        goto out;
    }
    memory_barrier();
    if (!command_configure_hid(controller, device)) { goto out; }
    if (device->configure_endpoint_completions == UINT32_MAX) { goto out; }
    for (index = 0U; index < configuration.endpoint_count; ++index) {
        device->hid_ring_physical[index] = rings[index];
        rings[index] = 0ULL;
    }
    device->hid_configuration = configuration;
    ++device->configure_endpoint_completions;
    device->device_configured = true;
    device->hid_endpoint_ready = true;
    success = true;
out:
    for (index = 0U; index < XHCI_MAX_HID_ENDPOINTS; ++index) {
        frame_release(rings[index]);
    }
    return success;
}

static bool discover_device_descriptors(struct xhci_controller *controller, struct xhci_addressed_device *device) {
    void *buffer_virtual = NULL;
    uint8_t *bytes;
    struct xhci_usb_descriptor_facts facts = {0U};
    uint16_t actual;
    uint16_t descriptor_max_packet;
    uint16_t configuration_length;

    if ((device == NULL) || !device->addressed || device->descriptors_ready ||
        (device->descriptor_buffer_physical != 0ULL) ||
        !frame_alloc_zero(&device->descriptor_buffer_physical,
                          &buffer_virtual)) {
        return false;
    }
    (void)buffer_virtual;
    if (!ep0_submit_get_descriptor(controller, device, XHCI_USB_DESCRIPTOR_DEVICE, 0U, 8U,
                                   &actual) ||
        (actual != 8U) || !descriptor_buffer_bytes(device, &bytes) ||
        !xhci_validate_device_descriptor_prefix(bytes, actual, device->speed,
                                                &descriptor_max_packet)) {
        goto fail;
    }
    if ((descriptor_max_packet != device->ep0_max_packet) &&
        !command_evaluate_ep0(controller, device, descriptor_max_packet)) {
        goto fail;
    }
    if (!ep0_submit_get_descriptor(controller, device, XHCI_USB_DESCRIPTOR_DEVICE, 0U, 18U,
                                   &actual) ||
        (actual != 18U) || !descriptor_buffer_bytes(device, &bytes) ||
        !xhci_validate_device_descriptor(bytes, actual, device->speed, &facts)) {
        goto fail;
    }
    if (!ep0_submit_get_descriptor(controller, device, XHCI_USB_DESCRIPTOR_CONFIGURATION,
                                   0U, 9U, &actual) ||
        (actual != 9U) || !descriptor_buffer_bytes(device, &bytes) ||
        !xhci_configuration_total_length(bytes, actual,
                                         &configuration_length)) {
        goto fail;
    }
    if (!ep0_submit_get_descriptor(controller, device, XHCI_USB_DESCRIPTOR_CONFIGURATION,
                                   0U, configuration_length, &actual) ||
        !descriptor_buffer_bytes(device, &bytes) ||
        !xhci_validate_configuration_descriptor(bytes, actual, &facts) ||
        (actual < configuration_length)) {
        goto fail;
    }
    device->descriptors = facts;
    device->descriptors_ready = true;
    return true;

fail:
    device->descriptors_ready = false;
    frame_release(device->descriptor_buffer_physical);
    device->descriptor_buffer_physical = 0ULL;
    return false;
}

static bool initialize_controller(struct xhci_controller *controller,
                                  const struct pci_device *device,
                                  uint8_t controller_index) {
    struct pci_bar bar;
    volatile void *mapping = NULL;
    volatile uint8_t *base;
    uint32_t operational;
    uint8_t port;
    bool success = false;

    XHCI_M61_BEGIN();
    if ((controller == NULL) || (device == NULL) ||
        (controller_index >= XHCI_MAX_CONTROLLERS)) {
        XHCI_M61_RETURN_FALSE(XHCI_M61_FALSE_INVALID_STATE);
    }
    state_clear(&controller->state);
    runtime_clear(controller);
    controller->state.device = *device;
    controller->state.controller_index = controller_index;
    controller->status = XHCI_CONTROLLER_DISCOVERED;
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_CONTROLLER_DISCOVERY);
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_BAR_VALIDATION);
    if (!pci_get_bar(&controller->state.device, 0U, &bar) || !bar.memory) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_INVALID_BAR);
    }
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_PCI_ENABLE);
    if (!pci_enable_memory_bus_master(&controller->state.device)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_PCI_ENABLE);
    }
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_MMIO_MAP);
    if (!vmm_map_mmio_region(bar.base, XHCI_MMIO_WINDOW_SIZE, &mapping)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_MMIO_MAP);
    }
#else
    if (!pci_get_bar(&controller->state.device, 0U, &bar) ||
        !bar.memory || !pci_enable_memory_bus_master(&controller->state.device) ||
        !vmm_map_mmio_region(bar.base, XHCI_MMIO_WINDOW_SIZE, &mapping)) {
        goto out;
    }
#endif
    base = (volatile uint8_t *)mapping;
    controller->state.mmio_physical = bar.base;
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_CAPABILITY_PARSE);
    if (!xhci_parse_capabilities(base, XHCI_MMIO_WINDOW_SIZE,
                                 &controller->state.capabilities)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_CAPABILITIES);
    }
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_LEGACY_HANDOFF);
    if (!legacy_handoff(base, &controller->state.capabilities,
                        &controller->state.legacy_handoff_complete)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_LEGACY_HANDOFF);
    }
#else
    if (!xhci_parse_capabilities(base, XHCI_MMIO_WINDOW_SIZE,
                                 &controller->state.capabilities) ||
        !legacy_handoff(base, &controller->state.capabilities,
                        &controller->state.legacy_handoff_complete)) {
        goto out;
    }
#endif
    operational = controller->state.capabilities.capability_length;
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_HALT_COMMAND);
    mmio_write32(base, operational + 0x00U,
                 mmio_read32(base, operational + 0x00U) & ~XHCI_USBCMD_RUN);
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_HALT_WAIT);
    if (!wait_mask(base, operational + 0x04U, XHCI_USBSTS_HALTED, true)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_HALT);
    }
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_RESET_COMMAND);
    mmio_write32(base, operational + 0x00U,
                 mmio_read32(base, operational + 0x00U) | XHCI_USBCMD_RESET);
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_RESET_WAIT);
    if (!wait_mask(base, operational + 0x00U, XHCI_USBCMD_RESET, false)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_RESET);
    }
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_CONTROLLER_READY_WAIT);
    if (!wait_mask(base, operational + 0x04U, XHCI_USBSTS_NOT_READY, false)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_CONTROLLER_NOT_READY);
    }
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_RINGS_SETUP);
    if (!rings_initialize(controller, base, &controller->state.capabilities, &controller->state)) {
        XHCI_M61_FAIL_PRESERVING_REASON(XHCI_M61_FALSE_RINGS_SETUP);
    }
#else
    if (!wait_mask(base, operational + 0x00U, XHCI_USBCMD_RESET, false) ||
        !wait_mask(base, operational + 0x04U, XHCI_USBSTS_NOT_READY, false) ||
        !rings_initialize(controller, base, &controller->state.capabilities, &controller->state)) {
        goto out;
    }
#endif
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_START_COMMAND);
    mmio_write32(base, operational + 0x00U,
                 mmio_read32(base, operational + 0x00U) | XHCI_USBCMD_RUN);
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_START_WAIT);
    if (!wait_mask(base, operational + 0x04U, XHCI_USBSTS_HALTED, false)) {
        XHCI_M61_FAIL(XHCI_M61_FALSE_START);
    }
    controller->state.controller_running = true;
    controller->status = XHCI_CONTROLLER_RUNNING;
    XHCI_M61_PROGRESS(XHCI_M61_PROGRESS_PORT_SCAN);
    for (port = 0U; port < controller->state.capabilities.max_ports; ++port) {
        const uint32_t portsc = mmio_read32(
            base, operational + XHCI_PORT_BASE +
                  ((uint32_t)port * XHCI_PORT_STRIDE));
        if ((portsc & 1U) != 0U) { controller->state.connected_ports |= 1ULL << port; }
    }
    success = true;

out:
    if ((!success) && (mapping != NULL)) {
        (void)vmm_unmap_mmio_region(mapping, XHCI_MMIO_WINDOW_SIZE);
    }
    if (!success) {
        rings_release(controller, &controller->state);
        controller->state.controller_running = false;
        runtime_clear(controller);
    }
    return success;
}

static bool xhci_state_identity_matches(
    const struct xhci_state *left, const struct xhci_state *right) {
    return (left != NULL) && (right != NULL) &&
           (left->controller_index == right->controller_index) &&
           (left->device.bdf.bus == right->device.bdf.bus) &&
           (left->device.bdf.device == right->device.bdf.device) &&
           (left->device.bdf.function == right->device.bdf.function) &&
           (left->mmio_physical == right->mmio_physical) &&
           (left->command_ring_physical == right->command_ring_physical) &&
           (left->event_ring_physical == right->event_ring_physical);
}

static struct xhci_controller *controller_from_state(
    const struct xhci_state *state) {
    struct xhci_controller *controller;
    if ((state == NULL) ||
        (state->controller_index >= controller_registry_count)) {
        return NULL;
    }
    controller = &controller_registry[state->controller_index];
    if ((controller->status != XHCI_CONTROLLER_RUNNING) ||
        !controller->state.controller_running ||
        !xhci_state_identity_matches(state, &controller->state)) {
        return NULL;
    }
    return controller;
}

bool xhci_init(struct xhci_state *state) {
    const struct boring_pci_inventory *inventory;
    struct pci_device devices[XHCI_MAX_CONTROLLERS];
    uint8_t discovered = 0U;
    uint8_t index;
    bool truncated = false;
    bool all_running = true;

    if ((state == NULL) || (controller_registry_count != 0U)) {
        return false;
    }
    inventory = boring_pci_inventory_get();
    if ((inventory == NULL) ||
        !xhci_collect_controller_devices(inventory, devices,
                                         &discovered, &truncated) ||
        (discovered == 0U)) {
        return false;
    }
    controller_registry_count = discovered;
    controller_registry_truncated = truncated;
    if (truncated) { all_running = false; }
    for (index = 0U; index < discovered; ++index) {
        if (!initialize_controller(&controller_registry[index],
                                   &devices[index], index)) {
            all_running = false;
        }
    }
    *state = controller_registry[0].state;
    return all_running && controller_registry[0].state.controller_running;
}

uint8_t xhci_controller_count(void) {
    return controller_registry_count;
}

bool xhci_controller_registry_truncated(void) {
    return controller_registry_truncated;
}

struct xhci_state *xhci_get_controller(uint8_t index) {
    if (index >= controller_registry_count) { return NULL; }
    return &controller_registry[index].state;
}

enum xhci_controller_init_status xhci_get_controller_status(uint8_t index) {
    if (index >= controller_registry_count) { return XHCI_CONTROLLER_EMPTY; }
    return controller_registry[index].status;
}


bool xhci_address_connected(struct xhci_state *state) {
    uint8_t port_index;
    if ((state == NULL) || !active_state.controller_running ||
        (runtime_state.mmio == NULL) || (runtime_state.dcbaa == NULL) ||
        (active_state.addressed_count != 0U)) {
        return false;
    }
    for (port_index = 0U;
         port_index < active_state.capabilities.max_ports; ++port_index) {
        if ((active_state.connected_ports & (1ULL << port_index)) == 0ULL) {
            continue;
        }
        if (active_state.addressed_count == XHCI_MAX_ADDRESSED_DEVICES) {
            active_state.addressing_truncated = true;
            break;
        }
        if (!address_root_port(
                (uint8_t)(port_index + 1U),
                &active_state.addressed[active_state.addressed_count])) {
            *state = active_state;
            return false;
        }
        ++active_state.addressed_count;
    }
    *state = active_state;
    return active_state.addressed_count != 0U;
}

bool xhci_discover_descriptors(struct xhci_state *state) {
    uint8_t index;
    if ((state == NULL) || !active_state.controller_running ||
        (runtime_state.mmio == NULL) || (active_state.addressed_count == 0U)) {
        return false;
    }
    for (index = 0U; index < active_state.addressed_count; ++index) {
        if (!discover_device_descriptors(&active_state.addressed[index])) {
            *state = active_state;
            return false;
        }
    }
    *state = active_state;
    return true;
}

bool xhci_configure_hid_devices(struct xhci_state *state) {
    uint8_t index;
    if ((state == NULL) || !active_state.controller_running ||
        (runtime_state.mmio == NULL) || (active_state.addressed_count == 0U)) {
        return false;
    }
    for (index = 0U; index < active_state.addressed_count; ++index) {
        if (!configure_hid_device(&active_state.addressed[index])) {
            *state = active_state;
            return false;
        }
    }
    *state = active_state;
    return true;
}

const struct xhci_state *xhci_get_state(void) {
    return active_state.controller_running ? &active_state : NULL;
}
