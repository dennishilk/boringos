#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/boot_console.h>
#include <boring/boringfs_vfs.h>
#include <boring/cpu.h>
#include <boring/cpu_inventory.h>
#include <boring/descriptor.h>
#include <boring/exception.h>
#include <boring/framebuffer.h>
#include <boring/framebuffer_user.h>
#include <boring/heap.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/irq.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/smbios.h>
#include <boring/syscall_test.h>
#include <boring/timer.h>
#include <boring/usb_mass_storage.h>
#include <boring/vmm.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

#ifndef BORING_M61_PHYSICAL_BREADCRUMBS
#error "M61 physical trace must stay candidate-build gated"
#endif

#define TRACE_LAST_STAGE 27U
#define EARLY_IDT_GATE_INTERRUPT 0x8eU
#define EARLY_IDT_IST_NONE 0U
#define EARLY_IDT_IST1 1U

struct m61_early_idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct m61_early_idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct m61_early_idt_entry) == 16U,
               "M61 emergency IDT entry must be 16 bytes");
_Static_assert(sizeof(struct m61_early_idtr) == 10U,
               "M61 emergency IDTR must be 10 bytes");

extern const uintptr_t
    x86_64_exception_stub_table[X86_64_EXCEPTION_VECTOR_COUNT];
void x86_64_load_idt(const struct m61_early_idtr *idtr);
void x86_64_store_idt(struct m61_early_idtr *idtr);
uint16_t x86_64_read_cs(void);

const char boring_m61_physical_breadcrumbs_enabled[] =
    "M61 metadata-only framebuffer acquisition and safe boot console enabled";
#ifdef BORING_M61_EARLY_FAULT_TEST
const char boring_m61_early_fault_test_enabled[] =
    "M61 controlled pre-exception-init fault test enabled";
#endif

static struct m61_early_idt_entry
    early_idt[X86_64_EXCEPTION_VECTOR_COUNT] __attribute__((aligned(16)));
static const struct boring_framebuffer *fb;
static volatile bool early_containment_active;
static bool serial_ready;
static bool wm_ready;
static bool terminal_ready;
static bool display_initial_presented;
static bool desktop_presented;

static void emergency_halt(void) __attribute__((noreturn));
static void emergency_halt(void) {
    x86_64_interrupts_disable();
    x86_64_halt_forever();
}

static void early_idt_set_gate(uint8_t vector, uintptr_t handler,
                               uint16_t selector, uint8_t ist) {
    struct m61_early_idt_entry *const entry = &early_idt[vector];
    const uint64_t address = (uint64_t)handler;

    entry->offset_low = (uint16_t)(address & 0xffffULL);
    entry->selector = selector;
    entry->ist = ist;
    entry->type_attributes = (uint8_t)EARLY_IDT_GATE_INTERRUPT;
    entry->offset_middle = (uint16_t)((address >> 16U) & 0xffffULL);
    entry->offset_high = (uint32_t)((address >> 32U) & 0xffffffffULL);
    entry->reserved = 0U;
}

static bool early_idt_load(uint16_t selector, uint8_t ist) {
    struct m61_early_idtr requested;
    struct m61_early_idtr active;
    size_t index;

    if (selector == 0U) {
        return false;
    }
    for (index = 0U;
         index < (size_t)X86_64_EXCEPTION_VECTOR_COUNT; ++index) {
        if (x86_64_exception_stub_table[index] == (uintptr_t)0U) {
            return false;
        }
        early_idt_set_gate((uint8_t)index,
                           x86_64_exception_stub_table[index],
                           selector, ist);
    }

    requested.limit = (uint16_t)(sizeof(early_idt) - 1U);
    requested.base = (uint64_t)(uintptr_t)&early_idt[0];
    active.limit = 0U;
    active.base = 0ULL;
    x86_64_load_idt(&requested);
    x86_64_store_idt(&active);
    return (active.limit == requested.limit) &&
           (active.base == requested.base);
}

static bool early_containment_install(void) {
    uint16_t selector = x86_64_read_cs();

    early_containment_active = true;
    if (!early_idt_load(selector, (uint8_t)EARLY_IDT_IST_NONE)) {
        return false;
    }

    /*
     * descriptor_init() is static-only and dependency-free. In this
     * diagnostic build it also supplies IST1, giving #PF/#GP/#UD/#DF a
     * dedicated stack before any GOP memory is touched.
     */
    if (!descriptor_init()) {
        return false;
    }
    selector = x86_64_read_cs();
    return early_idt_load(selector, (uint8_t)EARLY_IDT_IST1);
}

static void serial_stage(uint32_t stage_number, char mark,
                         const char *label) {
    if (!serial_ready) {
        return;
    }
    serial_write_string("M61 TRACE [");
    if (stage_number < 10U) {
        serial_write_string("0");
    }
    serial_write_u64((uint64_t)stage_number);
    serial_write_string(mark == '>' ? ">] " :
                        mark == '+' ? "+] " : "!] ");
    serial_write_string(label);
    serial_write_string("\n");
}

static const char *trace_failure_reason(uint32_t stage_number) {
    switch (stage_number) {
        case 2U: return "CPU inventory did not complete";
        case 3U: return "PCI inventory did not complete";
        case 4U: return "SMBIOS discovery did not complete";
        case 5U: return "pmm_init returned false";
        case 6U: return "vmm_init returned false";
        case 7U: return "heap_init returned false";
        case 8U: return "exception_init returned false";
        case 10U: return "input initialization returned false";
        case 11U: return "irq_init returned false";
        case 12U: return "timer_init returned false";
        case 13U: return "xhci_init returned false";
        case 14U: return "USB addressing returned false";
        case 15U: return "USB descriptor discovery returned false";
        case 16U: return "USB HID configuration returned false";
        case 17U: return "USB mass storage returned false";
        case 18U: return "BoringFS root mount returned failure";
        case 19U: return "boring-init process naming failed";
        case 20U: return "boring-display process naming failed";
        case 21U: return "boring-display framebuffer claim failed";
        case 22U: return "boring.display service registration failed";
        case 23U: return "boring-display initial present failed";
        case 24U: return "BoringWM process naming failed";
        case 25U: return "boring.wm service registration failed";
        case 26U: return "desktop framebuffer present failed";
        case 27U: return "automatic terminal process naming failed";
        default: return "stage returned failure";
    }
}

static bool trace_boot_stage(uint32_t stage_number,
                             enum boring_boot_console_stage *stage) {
    if (stage == NULL) {
        return false;
    }
    switch (stage_number) {
        case 2U: *stage = BORING_BOOT_STAGE_CPU_INVENTORY; return true;
        case 3U: *stage = BORING_BOOT_STAGE_PCI_INVENTORY; return true;
        case 4U: *stage = BORING_BOOT_STAGE_SMBIOS; return true;
        case 5U: *stage = BORING_BOOT_STAGE_PMM; return true;
        case 6U: *stage = BORING_BOOT_STAGE_VMM; return true;
        case 7U: *stage = BORING_BOOT_STAGE_HEAP; return true;
        case 8U: *stage = BORING_BOOT_STAGE_EXCEPTIONS; return true;
        case 10U: *stage = BORING_BOOT_STAGE_INPUT; return true;
        case 11U: *stage = BORING_BOOT_STAGE_IRQ; return true;
        case 12U: *stage = BORING_BOOT_STAGE_PIT; return true;
        case 13U: *stage = BORING_BOOT_STAGE_XHCI; return true;
        case 14U: *stage = BORING_BOOT_STAGE_USB_ADDRESSING; return true;
        case 15U: *stage = BORING_BOOT_STAGE_USB_DESCRIPTORS; return true;
        case 16U: *stage = BORING_BOOT_STAGE_USB_HID; return true;
        case 17U: *stage = BORING_BOOT_STAGE_USB_MASS_STORAGE; return true;
        case 18U: *stage = BORING_BOOT_STAGE_BORINGFS_ROOT; return true;
        case 19U: *stage = BORING_BOOT_STAGE_BORING_INIT; return true;
        case 20U:
        case 21U:
        case 22U:
        case 23U:
            *stage = BORING_BOOT_STAGE_BORING_DISPLAY;
            return true;
        case 24U:
        case 25U:
            *stage = BORING_BOOT_STAGE_BORING_WM;
            return true;
        case 26U: *stage = BORING_BOOT_STAGE_DESKTOP_PRESENT; return true;
        case 27U: *stage = BORING_BOOT_STAGE_AUTOMATIC_TERMINAL; return true;
        default: return false;
    }
}

static void trace_stage(uint32_t stage_number, char mark,
                        const char *label) {
    enum boring_boot_console_stage boot_stage;
    bool apply = true;

    if ((stage_number == 0U) || (stage_number > TRACE_LAST_STAGE) ||
        (label == NULL)) {
        return;
    }
    serial_stage(stage_number, mark, label);
    if (!trace_boot_stage(stage_number, &boot_stage)) {
        return;
    }

    /* Naming/claim/service success is not display or WM readiness. */
    if (((stage_number >= 20U) && (stage_number <= 24U)) && (mark == '+')) {
        apply = false;
    }
    if ((stage_number >= 21U) && (stage_number <= 23U) && (mark == '>')) {
        apply = false;
    }
    if (!apply) {
        return;
    }

    if (mark == '>') {
        (void)boring_boot_console_pending(boot_stage);
    } else if (mark == '+') {
        (void)boring_boot_console_ok(boot_stage);
    } else {
        (void)boring_boot_console_fail(
            boot_stage, trace_failure_reason(stage_number));
    }
}

static void serial_surface(uint64_t index,
                           const struct boring_framebuffer *surface,
                           bool selected) {
    if ((!serial_ready) || (surface == NULL)) {
        return;
    }
    serial_write_string("M61 FRAMEBUFFER CANDIDATE index=");
    serial_write_u64(index);
    serial_write_string(" selected=");
    serial_write_string(selected ? "yes" : "no");
    serial_write_string(" width=");
    serial_write_u64(surface->width);
    serial_write_string(" height=");
    serial_write_u64(surface->height);
    serial_write_string(" pitch=");
    serial_write_u64(surface->pitch);
    serial_write_string(" bpp=");
    serial_write_u64((uint64_t)surface->bpp);
    serial_write_string(" model=");
    serial_write_u64((uint64_t)surface->memory_model);
    serial_write_string(" masks=");
    serial_write_u64((uint64_t)surface->red_mask_size);
    serial_write_string("/");
    serial_write_u64((uint64_t)surface->red_mask_shift);
    serial_write_string(",");
    serial_write_u64((uint64_t)surface->green_mask_size);
    serial_write_string("/");
    serial_write_u64((uint64_t)surface->green_mask_shift);
    serial_write_string(",");
    serial_write_u64((uint64_t)surface->blue_mask_size);
    serial_write_string("/");
    serial_write_u64((uint64_t)surface->blue_mask_shift);
    serial_write_string("\n");
}

static bool same_surface(const struct boring_framebuffer *first,
                         const struct boring_framebuffer *second) {
    if ((first == NULL) || (second == NULL)) {
        return false;
    }
    return (first->address == second->address) &&
           (first->width == second->width) &&
           (first->height == second->height) &&
           (first->pitch == second->pitch) &&
           (first->bpp == second->bpp) &&
           (first->memory_model == second->memory_model);
}

static void acquire_framebuffers(void) {
    const enum boring_framebuffer_status status =
        boring_framebuffer_boot_init();
    const uint64_t count = boring_m61_framebuffer_count();
    uint64_t selected_index = UINT64_MAX;
    uint64_t index;

    serial_write_string("M61 FRAMEBUFFER COUNT=");
    serial_write_u64(count);
    serial_write_string("\n");
    if (status != BORING_FRAMEBUFFER_STATUS_READY) {
        serial_write_string("M61 FRAMEBUFFER TRACE UNAVAILABLE status=");
        serial_write_u64((uint64_t)status);
        serial_write_string("\n");
        return;
    }
    fb = boring_framebuffer_get();
    if (!boring_framebuffer_surface_valid(fb)) {
        fb = NULL;
        serial_write_string("M61 FRAMEBUFFER TRACE INVALID SELECTED SURFACE\n");
        return;
    }

    for (index = 0ULL; index < count; ++index) {
        struct boring_framebuffer candidate;
        bool selected;

        if (!boring_m61_framebuffer_get(index, &candidate)) {
            serial_write_string("M61 FRAMEBUFFER CANDIDATE index=");
            serial_write_u64(index);
            serial_write_string(" valid=no\n");
            continue;
        }
        selected = (selected_index == UINT64_MAX) &&
                   same_surface(&candidate, fb);
        if (selected) {
            selected_index = index;
        }
        serial_surface(index, &candidate, selected);
    }

    serial_write_string("M61 FRAMEBUFFER DIAGNOSTIC WRITES BYPASSED count=");
    serial_write_u64(count);
    serial_write_string(" selected_index=");
    serial_write_u64(selected_index);
    serial_write_string("\n");
}

static bool ends_with(const char *value, const char *ending) {
    size_t value_length = 0U;
    size_t ending_length = 0U;
    size_t index;

    if ((value == NULL) || (ending == NULL)) {
        return false;
    }
    while ((value[value_length] != '\0') && (value_length < 96U)) {
        ++value_length;
    }
    while ((ending[ending_length] != '\0') && (ending_length < 96U)) {
        ++ending_length;
    }
    if ((value_length >= 96U) || (ending_length >= 96U) ||
        (value_length < ending_length)) {
        return false;
    }
    for (index = 0U; index < ending_length; ++index) {
        if (value[value_length - ending_length + index] != ending[index]) {
            return false;
        }
    }
    return true;
}

static bool same_bytes(const char *value, size_t value_length,
                       const char *expected, size_t expected_length) {
    size_t index;

    if ((value == NULL) || (expected == NULL) ||
        (value_length != expected_length)) {
        return false;
    }
    for (index = 0U; index < value_length; ++index) {
        if (value[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

void __real_serial_init(void);
void __wrap_serial_init(void);
void __real_boring_cpu_inventory_init(void);
void __wrap_boring_cpu_inventory_init(void);
void __real_boring_pci_inventory_init(void);
void __wrap_boring_pci_inventory_init(void);
void __real_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *,
    const struct boring_limine_memmap_response *);
void __wrap_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *,
    const struct boring_limine_memmap_response *);
bool __real_pmm_init(const struct boring_limine_memmap_response *);
bool __wrap_pmm_init(const struct boring_limine_memmap_response *);
bool __real_vmm_init(const struct boring_limine_hhdm_response *,
                     const struct boring_limine_paging_mode_response *,
                     const struct boring_limine_memmap_response *);
bool __wrap_vmm_init(const struct boring_limine_hhdm_response *,
                     const struct boring_limine_paging_mode_response *,
                     const struct boring_limine_memmap_response *);
bool __real_heap_init(void);
bool __wrap_heap_init(void);
bool __real_exception_init(void);
bool __wrap_exception_init(void);
void __real_syscall_test_run(void);
void __wrap_syscall_test_run(void);
bool __real_boring_input_init(void);
bool __wrap_boring_input_init(void);
bool __real_irq_init(void);
bool __wrap_irq_init(void);
bool __real_timer_init(uint32_t);
bool __wrap_timer_init(uint32_t);
bool __real_xhci_init(struct xhci_state *);
bool __wrap_xhci_init(struct xhci_state *);
bool __real_xhci_address_connected(struct xhci_state *);
bool __wrap_xhci_address_connected(struct xhci_state *);
bool __real_xhci_discover_descriptors(struct xhci_state *);
bool __wrap_xhci_discover_descriptors(struct xhci_state *);
bool __real_xhci_configure_hid_devices_mixed(struct xhci_state *);
bool __wrap_xhci_configure_hid_devices_mixed(struct xhci_state *);
bool __real_usb_mass_storage_init(struct xhci_state *);
bool __wrap_usb_mass_storage_init(struct xhci_state *);
enum vfs_result __real_boringfs_vfs_create_writable(
    const struct block_device *, uint64_t, struct boringfs_vfs **,
    struct boringfs_validation_error *);
enum vfs_result __wrap_boringfs_vfs_create_writable(
    const struct block_device *, uint64_t, struct boringfs_vfs **,
    struct boringfs_validation_error *);
bool __real_process_set_name(struct process *, const char *);
bool __wrap_process_set_name(struct process *, const char *);
enum boring_framebuffer_user_result __real_boring_framebuffer_user_claim(
    uint64_t, struct boring_display_scanout_info *);
enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_claim(
    uint64_t, struct boring_display_scanout_info *);
enum boring_framebuffer_user_result __real_boring_framebuffer_user_present(
    struct process *, uint32_t);
enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(
    struct process *, uint32_t);
enum boring_ipc_result __real_boring_ipc_service_register(
    struct process *, const char *, size_t, uint32_t *);
enum boring_ipc_result __wrap_boring_ipc_service_register(
    struct process *, const char *, size_t, uint32_t *);
void __real_x86_64_exception_dispatch(const struct x86_64_trap_frame *);
void __wrap_x86_64_exception_dispatch(const struct x86_64_trap_frame *);

void __wrap_serial_init(void) {
    if (!early_containment_install()) {
        emergency_halt();
    }
    __real_serial_init();
    serial_ready = true;
    serial_write_string("M61 EARLY EXCEPTION CONTAINMENT ARMED IST1\n");
#ifdef BORING_M61_EARLY_FAULT_TEST
    serial_write_string("M61 CONTROLLED EARLY FAULT ARMED vector=6\n");
    __asm__ volatile("ud2");
    emergency_halt();
#endif
    acquire_framebuffers();
    trace_stage(1U, '>', "SERIAL PROBE");
    trace_stage(1U, '+', "SERIAL PROBE");
}

void __wrap_boring_cpu_inventory_init(void) {
    const struct boring_cpu_inventory *inventory;

    trace_stage(2U, '>', "CPU INVENTORY");
    __real_boring_cpu_inventory_init();
    inventory = boring_cpu_inventory_get();
    trace_stage(2U,
                ((inventory != NULL) && (inventory->vendor[0] != '\0')) ?
                    '+' : '!',
                "CPU INVENTORY");
}

void __wrap_boring_pci_inventory_init(void) {
    const struct boring_pci_inventory *inventory;

    trace_stage(3U, '>', "PCI INVENTORY");
    __real_boring_pci_inventory_init();
    inventory = boring_pci_inventory_get();
    trace_stage(3U,
                ((inventory != NULL) && inventory->complete) ? '+' : '!',
                "PCI INVENTORY");
}

void __wrap_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memmap) {
    const struct boring_platform_identity *identity;

    trace_stage(4U, '>', "SMBIOS");
    __real_boring_smbios_boot_init(hhdm, memmap);
    identity = boring_platform_identity_get();
    trace_stage(4U,
                ((identity != NULL) && identity->available &&
                 identity->complete) ? '+' : '!',
                "SMBIOS");
}

bool __wrap_pmm_init(const struct boring_limine_memmap_response *memmap) {
    bool result;

    trace_stage(5U, '>', "PMM INIT");
    result = __real_pmm_init(memmap);
    trace_stage(5U, result ? '+' : '!', "PMM INIT");
    return result;
}

bool __wrap_vmm_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_paging_mode_response *paging,
    const struct boring_limine_memmap_response *memmap) {
    bool result;

    trace_stage(6U, '>', "VMM INIT");
    result = __real_vmm_init(hhdm, paging, memmap);
    trace_stage(6U, result ? '+' : '!', "VMM INIT");
    return result;
}

bool __wrap_heap_init(void) {
    bool result;

    trace_stage(7U, '>', "HEAP INIT");
    result = __real_heap_init();
    trace_stage(7U, result ? '+' : '!', "HEAP INIT");
    return result;
}

bool __wrap_exception_init(void) {
    bool result;

    trace_stage(8U, '>', "EXCEPTIONS");
    result = __real_exception_init();
    if (result) {
        early_containment_active = false;
    }
    trace_stage(8U, result ? '+' : '!', "EXCEPTIONS");
    return result;
}

void __wrap_syscall_test_run(void) {
    trace_stage(9U, '>', "DESKTOP HARNESS");
    __real_syscall_test_run();
    trace_stage(9U, '+', "DESKTOP HARNESS");
}

bool __wrap_boring_input_init(void) {
    bool result;

    trace_stage(10U, '>', "INPUT CORE");
    result = __real_boring_input_init();
    trace_stage(10U, result ? '+' : '!', "INPUT CORE");
    return result;
}

bool __wrap_irq_init(void) {
    bool result;

    trace_stage(11U, '>', "IRQ INIT");
    result = __real_irq_init();
    trace_stage(11U, result ? '+' : '!', "IRQ INIT");
    return result;
}

bool __wrap_timer_init(uint32_t frequency_hz) {
    bool result;

    trace_stage(12U, '>', "PIT TIMER");
    result = __real_timer_init(frequency_hz);
    trace_stage(12U, result ? '+' : '!', "PIT TIMER");
    return result;
}

bool __wrap_xhci_init(struct xhci_state *state) {
    bool result;

    trace_stage(13U, '>', "XHCI CONTROLLER");
    result = __real_xhci_init(state);
    trace_stage(13U, result ? '+' : '!', "XHCI CONTROLLER");
    return result;
}

bool __wrap_xhci_address_connected(struct xhci_state *state) {
    bool result;

    trace_stage(14U, '>', "XHCI ADDRESS");
    result = __real_xhci_address_connected(state);
    trace_stage(14U, result ? '+' : '!', "XHCI ADDRESS");
    return result;
}

bool __wrap_xhci_discover_descriptors(struct xhci_state *state) {
    bool result;

    trace_stage(15U, '>', "XHCI DESCRIPTORS");
    result = __real_xhci_discover_descriptors(state);
    trace_stage(15U, result ? '+' : '!', "XHCI DESCRIPTORS");
    return result;
}

bool __wrap_xhci_configure_hid_devices_mixed(struct xhci_state *state) {
    bool result;

    trace_stage(16U, '>', "XHCI HID CONFIG");
    result = __real_xhci_configure_hid_devices_mixed(state);
    trace_stage(16U, result ? '+' : '!', "XHCI HID CONFIG");
    return result;
}

bool __wrap_usb_mass_storage_init(struct xhci_state *state) {
    bool result;

    trace_stage(17U, '>', "USB MASS STORAGE");
    result = __real_usb_mass_storage_init(state);
    trace_stage(17U, result ? '+' : '!', "USB MASS STORAGE");
    return result;
}

enum vfs_result __wrap_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out,
    struct boringfs_validation_error *error) {
    enum vfs_result result;

    trace_stage(18U, '>', "BORINGFS ROOT");
    result = __real_boringfs_vfs_create_writable(
        device, id, out, error);
    trace_stage(18U, result == VFS_RESULT_OK ? '+' : '!', "BORINGFS ROOT");
    return result;
}

bool __wrap_process_set_name(struct process *process, const char *name) {
    uint32_t stage_number = 0U;
    const char *label = NULL;
    bool result;

    if (ends_with(name, "boring-init")) {
        stage_number = 19U;
        label = "BORING-INIT START";
    } else if (ends_with(name, "boring-display")) {
        stage_number = 20U;
        label = "BORING-DISPLAY START";
    } else if (ends_with(name, "boringwm")) {
        stage_number = 24U;
        label = "BORING-WM START";
    } else if (ends_with(name, "boring-terminal")) {
        stage_number = 27U;
        label = "TERMINAL START";
    }
    if (stage_number != 0U) {
        trace_stage(stage_number, '>', label);
    }
    result = __real_process_set_name(process, name);
    if (stage_number != 0U) {
        trace_stage(stage_number, result ? '+' : '!', label);
    }
    if ((stage_number == 27U) && result) {
        terminal_ready = true;
    }
    return result;
}

enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_claim(
    uint64_t pid, struct boring_display_scanout_info *info) {
    enum boring_framebuffer_user_result result;

    trace_stage(21U, '>', "DISPLAY FB CLAIM");
    result = __real_boring_framebuffer_user_claim(pid, info);
    trace_stage(21U,
                result == BORING_FRAMEBUFFER_USER_OK ? '+' : '!',
                "DISPLAY FB CLAIM");
    return result;
}

enum boring_ipc_result __wrap_boring_ipc_service_register(
    struct process *process, const char *name, size_t length,
    uint32_t *out) {
    uint32_t stage_number = 0U;
    const char *label = NULL;
    enum boring_ipc_result result;

    if (same_bytes(name, length, "boring.display", 14U)) {
        stage_number = 22U;
        label = "DISPLAY SERVICE";
    } else if (same_bytes(name, length, "boring.wm", 9U)) {
        stage_number = 25U;
        label = "WM SERVICE";
    }
    if (stage_number != 0U) {
        trace_stage(stage_number, '>', label);
    }
    result = __real_boring_ipc_service_register(
        process, name, length, out);
    if (stage_number != 0U) {
        trace_stage(stage_number,
                    result == BORING_IPC_RESULT_OK ? '+' : '!',
                    label);
    }
    if ((stage_number == 25U) && (result == BORING_IPC_RESULT_OK)) {
        wm_ready = true;
    }
    return result;
}

enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(
    struct process *process, uint32_t handle) {
    enum boring_framebuffer_user_result result;
    const bool final_present = wm_ready && terminal_ready;
    const uint32_t stage_number = final_present ? 26U : 23U;
    const char *const label = final_present ?
        "DESKTOP PRESENT" : "DISPLAY INITIAL PRESENT";

    if (desktop_presented) {
        return __real_boring_framebuffer_user_present(process, handle);
    }
    trace_stage(stage_number, '>', label);
    result = __real_boring_framebuffer_user_present(process, handle);
    if (result != BORING_FRAMEBUFFER_USER_OK) {
        trace_stage(stage_number, '!', label);
        return result;
    }

    if (final_present) {
        desktop_presented = true;
        serial_stage(stage_number, '+', label);
        boring_boot_console_desktop_handoff();
        return result;
    }

    (void)boring_boot_console_ok(BORING_BOOT_STAGE_BORING_DISPLAY);
    if (!display_initial_presented) {
        display_initial_presented = true;
        if (!boring_boot_console_activate(fb) && serial_ready) {
            serial_write_string(
                "BOOT-CONSOLE framebuffer unavailable; serial/POST fallback active\n");
        }
    } else {
        (void)boring_boot_console_refresh();
    }
    trace_stage(stage_number, '+', label);
    return result;
}

void __wrap_x86_64_exception_dispatch(
    const struct x86_64_trap_frame *frame) {
    if (early_containment_active) {
        if (serial_ready) {
            serial_write_string("M61 EARLY FAULT CONTAINED vector=");
            serial_write_u64(frame == NULL ? UINT64_MAX : frame->vector);
            serial_write_string(" framebuffer_write_active=no\n");
        }
        emergency_halt();
    }
    __real_x86_64_exception_dispatch(frame);
}
