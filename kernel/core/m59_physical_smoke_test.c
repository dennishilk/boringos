#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/cpu_inventory.h>
#include <boring/framebuffer.h>
#include <boring/graphics.h>
#include <boring/input.h>
#include <boring/m59_physical_smoke_test.h>
#include <boring/pci_inventory.h>
#include <boring/pixel_font.h>
#include <boring/pmm.h>
#include <boring/serial.h>
#include <boring/smbios.h>
#include <boring/vmm.h>
#include <boring/xhci.h>

#define M59_INPUT_OWNER_PID 59ULL
#define M59_POLL_LIMIT 16U
#define M59_GIB (1024ULL * 1024ULL * 1024ULL)

static const char *m59_text_or_unknown(const char *text) {
    return ((text != NULL) && (text[0] != '\0')) ? text : "unavailable";
}

static void m59_serial_key_value(const char *key, const char *value) {
    serial_write_string(key);
    serial_write_string(value);
    serial_write_string("\n");
}

static void m59_serial_u64(const char *key, uint64_t value) {
    serial_write_string(key);
    serial_write_u64(value);
    serial_write_string("\n");
}

static void m59_u64_text(uint64_t value, char buffer[32]) {
    char reverse[32];
    size_t count = 0U;
    size_t index;

    if (value == 0ULL) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    while ((value != 0ULL) && (count < sizeof(reverse))) {
        reverse[count] = (char)('0' + (char)(value % 10ULL));
        value /= 10ULL;
        ++count;
    }
    for (index = 0U; index < count; ++index) {
        buffer[index] = reverse[count - index - 1U];
    }
    buffer[count] = '\0';
}

static void m59_fb_line(const struct boring_framebuffer *surface,
                        uint64_t y, const char *label, const char *value,
                        uint32_t label_color, uint32_t value_color) {
    (void)boring_pixel_font_draw_text(surface, 24ULL, y, label, label_color);
    (void)boring_pixel_font_draw_text(surface, 190ULL, y, value, value_color);
}

static void m59_render_summary(const struct pmm_stats *pmm,
                               const struct boring_cpu_inventory *cpu,
                               const struct boring_platform_identity *platform,
                               const struct boring_pci_inventory *pci,
                               bool xhci_ready,
                               bool keyboard_detected,
                               bool pointer_detected,
                               bool input_observed) {
    const struct boring_framebuffer *surface = boring_framebuffer_get();
    char number[32];
    uint32_t background;
    uint32_t title;
    uint32_t label;
    uint32_t good;
    uint32_t quiet;
    uint64_t y = 24ULL;

    if (surface == NULL) { return; }
    background = boring_color_pack(surface, 8U, 12U, 16U);
    title = boring_color_pack(surface, 91U, 214U, 255U);
    label = boring_color_pack(surface, 185U, 198U, 208U);
    good = boring_color_pack(surface, 126U, 231U, 135U);
    quiet = boring_color_pack(surface, 235U, 189U, 95U);
    (void)boring_graphics_clear(surface, background);
    (void)boring_pixel_font_draw_text_scaled(surface, 24ULL, y,
                                              "BoringOS Physical Smoke",
                                              title, 2U);
    y += 34ULL;
    m59_fb_line(surface, y, "Kernel", "BoringKernel 0.0.59-dev", label, good);
    y += 14ULL;
    m59_fb_line(surface, y, "Arch", "x86_64", label, good);
    y += 14ULL;
    if (pmm != NULL) {
        m59_u64_text(pmm->usable_bytes, number);
        m59_fb_line(surface, y, "Memory bytes", number, label, good);
    } else {
        m59_fb_line(surface, y, "Memory bytes", "unavailable", label, quiet);
    }
    y += 14ULL;
    m59_fb_line(surface, y, "Memory >4GiB",
                ((pmm != NULL) && (pmm->usable_bytes > (4ULL * M59_GIB))) ?
                    "YES" : "NO / not exposed",
                label, ((pmm != NULL) &&
                        (pmm->usable_bytes > (4ULL * M59_GIB))) ? good : quiet);
    y += 14ULL;
    m59_fb_line(surface, y, "CPU",
                (cpu != NULL) ? m59_text_or_unknown(cpu->brand) : "unavailable",
                label, good);
    y += 14ULL;
    m59_fb_line(surface, y, "Machine",
                (platform != NULL && platform->available) ?
                    m59_text_or_unknown(platform->system_product) : "unavailable",
                label, (platform != NULL && platform->available) ? good : quiet);
    y += 14ULL;
    m59_fb_line(surface, y, "Board",
                (platform != NULL && platform->available) ?
                    m59_text_or_unknown(platform->board_product) : "unavailable",
                label, (platform != NULL && platform->available) ? good : quiet);
    y += 14ULL;
    if (pci != NULL) {
        m59_u64_text((uint64_t)pci->total, number);
        m59_fb_line(surface, y, "PCI devices", number, label,
                    pci->complete ? good : quiet);
    } else {
        m59_fb_line(surface, y, "PCI devices", "unavailable", label, quiet);
    }
    y += 14ULL;
    m59_fb_line(surface, y, "Framebuffer", "READY", label, good);
    y += 14ULL;
    m59_fb_line(surface, y, "xHCI", xhci_ready ? "READY" : "unavailable",
                label, xhci_ready ? good : quiet);
    y += 14ULL;
    m59_fb_line(surface, y, "Keyboard",
                keyboard_detected ? "DETECTED" : "not detected",
                label, keyboard_detected ? good : quiet);
    y += 14ULL;
    m59_fb_line(surface, y, "Pointer",
                pointer_detected ? "DETECTED" : "not detected",
                label, pointer_detected ? good : quiet);
    y += 14ULL;
    m59_fb_line(surface, y, "Canonical input",
                input_observed ? "EVENTS OBSERVED" : "no event observed",
                label, input_observed ? good : quiet);
    y += 14ULL;
    m59_fb_line(surface, y, "Storage writes", "DISABLED", label, good);
    y += 24ULL;
    (void)boring_pixel_font_draw_text_scaled(surface, 24ULL, y,
                                              "PHYSICAL SMOKE READY",
                                              good, 2U);
}

static bool m59_hid_facts(struct xhci_state *state,
                          bool *keyboard_detected,
                          bool *pointer_detected,
                          bool *all_addressed_hid) {
    uint8_t index;
    bool any_hid = false;

    if ((state == NULL) || (keyboard_detected == NULL) ||
        (pointer_detected == NULL) || (all_addressed_hid == NULL) ||
        (state->addressed_count == 0U)) {
        return false;
    }
    *keyboard_detected = false;
    *pointer_detected = false;
    *all_addressed_hid = true;
    for (index = 0U; index < state->addressed_count; ++index) {
        struct xhci_addressed_device *device = &state->addressed[index];
        struct xhci_hid_configuration configuration;
        void *virtual_address = NULL;
        uint8_t endpoint;

        if (!device->descriptors_ready ||
            !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                                   &virtual_address) ||
            !xhci_parse_hid_configuration(
                (const uint8_t *)virtual_address,
                device->descriptors.configuration_length,
                device->speed, &configuration)) {
            *all_addressed_hid = false;
            continue;
        }
        any_hid = true;
        for (endpoint = 0U; endpoint < configuration.endpoint_count; ++endpoint) {
            if (configuration.endpoints[endpoint].protocol == 1U) {
                *keyboard_detected = true;
            } else if ((configuration.endpoints[endpoint].protocol == 0U) ||
                       (configuration.endpoints[endpoint].protocol == 2U)) {
                *pointer_detected = true;
            }
        }
    }
    return any_hid;
}

static bool m59_runtime_input_evidence(const struct xhci_state *state,
                                       bool *keyboard_event,
                                       bool *pointer_event) {
    uint8_t device_index;

    if ((state == NULL) || (keyboard_event == NULL) ||
        (pointer_event == NULL)) {
        return false;
    }
    *keyboard_event = false;
    *pointer_event = false;
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device = &state->addressed[device_index];
        uint8_t endpoint;
        for (endpoint = 0U; endpoint < device->hid_configuration.endpoint_count;
             ++endpoint) {
            const struct xhci_hid_endpoint_descriptor *descriptor =
                &device->hid_configuration.endpoints[endpoint];
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint];
            if ((descriptor->protocol == 1U) &&
                ((runtime->key_presses != 0U) ||
                 (runtime->key_releases != 0U))) {
                *keyboard_event = true;
            } else if (((descriptor->protocol == 0U) ||
                        (descriptor->protocol == 2U)) &&
                       (runtime->pointer_reports != 0U)) {
                *pointer_event = true;
            }
        }
    }
    return *keyboard_event || *pointer_event;
}

void m59_physical_smoke_test_run(void) {
    struct pmm_stats pmm;
    struct xhci_state xhci;
    struct boring_input_stats input_stats;
    const struct boring_cpu_inventory *cpu = boring_cpu_inventory_get();
    const struct boring_platform_identity *platform =
        boring_platform_identity_get();
    const struct boring_pci_inventory *pci = boring_pci_inventory_get();
    bool pmm_ready = pmm_get_stats(&pmm);
    bool framebuffer_ready = boring_framebuffer_get() != NULL;
    bool xhci_ready = false;
    bool addressed = false;
    bool descriptors = false;
    bool keyboard_detected = false;
    bool pointer_detected = false;
    bool all_addressed_hid = false;
    bool hid_present = false;
    bool hid_configured = false;
    bool input_owned = false;
    bool keyboard_event = false;
    bool pointer_event = false;
    bool input_observed = false;
    uint32_t poll;

    serial_write_string("\n=== BoringOS Physical Smoke ===\n");
    m59_serial_key_value("Kernel: ", "BoringKernel 0.0.59-dev");
    m59_serial_key_value("Arch: ", "x86_64");
    if (pmm_ready) {
        m59_serial_u64("Memory usable bytes: ", pmm.usable_bytes);
        m59_serial_u64("Memory usable frames: ", pmm.usable_frames);
        m59_serial_key_value("Memory above 4GiB: ",
                             pmm.usable_bytes > (4ULL * M59_GIB) ? "YES" : "NO");
    } else {
        m59_serial_key_value("Memory: ", "UNAVAILABLE");
    }
    if (cpu != NULL) {
        m59_serial_key_value("CPU vendor: ", m59_text_or_unknown(cpu->vendor));
        m59_serial_key_value("CPU brand: ", m59_text_or_unknown(cpu->brand));
    } else {
        m59_serial_key_value("CPU: ", "UNAVAILABLE");
    }
    if ((platform != NULL) && platform->available) {
        m59_serial_key_value("Machine: ",
                             m59_text_or_unknown(platform->system_product));
        m59_serial_key_value("Board: ",
                             m59_text_or_unknown(platform->board_product));
        m59_serial_key_value("Firmware vendor: ",
                             m59_text_or_unknown(platform->firmware_vendor));
        m59_serial_key_value("Firmware version: ",
                             m59_text_or_unknown(platform->firmware_version));
    } else {
        m59_serial_key_value("SMBIOS: ", "UNAVAILABLE (optional)");
    }
    if (pci != NULL) {
        m59_serial_u64("PCI devices: ", (uint64_t)pci->total);
        m59_serial_key_value("PCI enumeration: ",
                             pci->complete ? "COMPLETE" : "PARTIAL");
    } else {
        m59_serial_key_value("PCI enumeration: ", "UNAVAILABLE");
    }
    m59_serial_key_value("Framebuffer: ",
                         framebuffer_ready ? "READY" : "UNAVAILABLE (optional)");

    xhci_ready = xhci_init(&xhci);
    m59_serial_key_value("xHCI: ", xhci_ready ? "READY" : "UNAVAILABLE/UNSUPPORTED");
    if (xhci_ready) {
        m59_serial_u64("xHCI connected root ports: ", xhci.connected_ports);
        addressed = xhci_address_connected(&xhci);
        if (addressed) {
            m59_serial_u64("USB addressed devices: ",
                           (uint64_t)xhci.addressed_count);
            m59_serial_key_value("USB addressing truncated: ",
                                 xhci.addressing_truncated ? "YES" : "NO");
            descriptors = xhci_discover_descriptors(&xhci);
            m59_serial_key_value("USB descriptors: ",
                                 descriptors ? "READY" : "PARTIAL/UNSUPPORTED");
        } else {
            m59_serial_key_value("USB devices: ", "NONE / ADDRESSING UNSUPPORTED");
        }
    }

    if (descriptors) {
        hid_present = m59_hid_facts(&xhci, &keyboard_detected,
                                    &pointer_detected, &all_addressed_hid);
    }
    m59_serial_key_value("USB keyboard: ",
                         keyboard_detected ? "DETECTED" : "NOT DETECTED");
    m59_serial_key_value("USB pointer: ",
                         pointer_detected ? "DETECTED" : "NOT DETECTED");

    if (hid_present && all_addressed_hid && boring_input_init() &&
        (boring_input_claim(M59_INPUT_OWNER_PID) == BORING_INPUT_RESULT_OK)) {
        input_owned = true;
        hid_configured = xhci_configure_hid_devices(&xhci);
        if (hid_configured) {
            serial_write_string("M59 USB input ready; inject or provide real USB input now.\n");
            for (poll = 0U; poll < M59_POLL_LIMIT; ++poll) {
                if (!xhci_poll_hid_reports(&xhci, 1U)) { break; }
                (void)m59_runtime_input_evidence(&xhci, &keyboard_event,
                                                 &pointer_event);
                if (boring_input_get_stats(&input_stats) &&
                    (input_stats.queued_events != 0U) &&
                    keyboard_event && pointer_event) {
                    input_observed = true;
                    break;
                }
            }
        }
    } else if (hid_present && !all_addressed_hid) {
        serial_write_string(
            "USB HID transport: DETECTION ONLY (mixed non-HID devices present; M59 does not own storage)\n");
    }
    if (input_owned) {
        if (boring_input_get_stats(&input_stats)) {
            m59_serial_u64("Canonical input queued events: ",
                           (uint64_t)input_stats.queued_events);
            m59_serial_u64("Canonical input dropped events: ",
                           input_stats.dropped_events);
            if (input_stats.queued_events != 0U) { input_observed = true; }
        }
        (void)boring_input_release(M59_INPUT_OWNER_PID);
    }
    m59_serial_key_value("USB HID configured: ",
                         hid_configured ? "YES" : "NO / NOT REQUIRED");
    m59_serial_key_value("Canonical input: ",
                         input_observed ? "EVENTS OBSERVED" : "NO EVENT OBSERVED");
    m59_serial_key_value("Storage writes: ", "DISABLED");

    m59_render_summary(pmm_ready ? &pmm : NULL, cpu, platform, pci,
                       xhci_ready, keyboard_detected, pointer_detected,
                       input_observed);
    serial_write_string("PHYSICAL SMOKE READY\n");

    if (pmm_ready && (cpu != NULL) && (pci != NULL) && pci->complete &&
        framebuffer_ready && xhci_ready && addressed && descriptors &&
        !xhci.addressing_truncated && keyboard_detected && pointer_detected &&
        hid_configured && input_observed && keyboard_event && pointer_event) {
        serial_write_string("M59 PHYSICAL SMOKE HARNESS PASSED\n");
    } else {
        serial_write_string(
            "M59 PHYSICAL SMOKE HARNESS: bounded diagnostic completion (optional/unsupported hardware may be absent)\n");
    }
    x86_64_halt_forever();
}
