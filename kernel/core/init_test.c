#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_boot.h>
#include <boring/elf_loader.h>
#include <boring/init_test.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/vmm.h>

#define INIT_EXPECTED_MODULE_PATH "/boot/user/boring-init.elf"
#define INIT_EXPECTED_MODULE_STRING "boringos-boring-init"
#define INIT_TEXT_VA 0x0000000040000000ULL
#define INIT_STACK_BASE 0x0000000040010000ULL
#define INIT_STACK_TOP 0x0000000040011000ULL

void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static void init_test_fail(const char *check) __attribute__((noreturn));
static void init_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("boring-init acceptance FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool canonical_higher(uintptr_t address) {
    return ((uint64_t)address >> 48U) == 0xffffULL;
}

static bool hhdm_pointer(const void *pointer) {
    struct vmm_stats stats;
    const uintptr_t address = (uintptr_t)pointer;

    return (pointer != NULL) && vmm_get_stats(&stats) &&
           canonical_higher(address) &&
           ((uint64_t)address >= stats.hhdm_offset);
}

static bool limine_string_equals(const char *actual, const char *expected) {
    size_t index = 0U;

    if (!hhdm_pointer(actual) || (expected == NULL)) {
        return false;
    }

    while (expected[index] != '\0') {
        if ((index >= 96U) || (actual[index] != expected[index])) {
            return false;
        }
        ++index;
    }

    return actual[index] == '\0';
}

static bool init_module_find(const struct boring_limine_module_response *response,
                             const uint8_t **bytes,
                             size_t *size) {
    const struct boring_limine_file *module;
    struct vmm_stats stats;
    uintptr_t address;
    uintptr_t last;

    if ((response == NULL) || (bytes == NULL) || (size == NULL) ||
        (response->module_count != 1ULL) ||
        !hhdm_pointer(response->modules) ||
        (response->modules[0] == NULL) ||
        !hhdm_pointer(response->modules[0]) ||
        !vmm_get_stats(&stats)) {
        return false;
    }

    module = response->modules[0];
    address = (uintptr_t)module->address;
    if (!hhdm_pointer(module->address) ||
        ((address & (uintptr_t)(BORING_ELF_PAGE_SIZE - 1ULL)) != 0U) ||
        (module->size == 0ULL) ||
        (module->size > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) ||
        (module->size > (uint64_t)SIZE_MAX) ||
        ((uint64_t)address > UINT64_MAX - (module->size - 1ULL))) {
        return false;
    }

    last = address + (uintptr_t)(module->size - 1ULL);
    if (!canonical_higher(last) ||
        ((uint64_t)address < stats.hhdm_offset) ||
        !limine_string_equals(module->path, INIT_EXPECTED_MODULE_PATH) ||
        !limine_string_equals(module->string, INIT_EXPECTED_MODULE_STRING)) {
        return false;
    }

    *bytes = (const uint8_t *)module->address;
    *size = (size_t)module->size;
    return true;
}

void init_test_run(void) {
    const struct boring_limine_module_response *modules =
        elf_boot_module_response();
    const uint8_t *module = NULL;
    size_t module_size = 0U;
    struct boring_elf_validation validation;
    struct boring_elf_image image;
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;
    struct process *process = NULL;

    serial_write_string("boring-init launch:\n");

    if (!init_module_find(modules, &module, &module_size)) {
        init_test_fail("boot-module-found");
    }
    serial_write_string("  boot-module-found: PASS\n");

    if (!boring_elf_validate(module, module_size,
                             (uintptr_t)INIT_STACK_BASE,
                             (size_t)BORING_ELF_PAGE_SIZE,
                             &validation) ||
        (validation.entry != (uintptr_t)INIT_TEXT_VA)) {
        init_test_fail("elf64-init-image");
    }
    serial_write_string("  elf64-init-image: PASS\n");

    if (!x86_64_enable_nx() || !x86_64_nx_enabled()) {
        init_test_fail("nx-enabled");
    }
    serial_write_string("  nx-enabled: PASS\n");

    if (!process_init() || !syscall_init() ||
        !process_create(&process) || (process == NULL) ||
        (process->pid != 1ULL)) {
        init_test_fail("process-created");
    }
    serial_write_string("  process-created: PASS\n");

    if (!boring_elf_load(process, module, module_size,
                         (uintptr_t)INIT_STACK_BASE,
                         (size_t)BORING_ELF_PAGE_SIZE, &image)) {
        init_test_fail("load-init-image");
    }
    serial_write_string("  load-init-image: PASS\n");

    if (!ring3_user_query_mapping(&process->address_space,
                                  image.entry, &entry_info) ||
        !entry_info.executable || entry_info.writable) {
        init_test_fail("entry-executable");
    }
    serial_write_string("  entry-executable: PASS\n");

    if (!ring3_user_query_mapping(&process->address_space,
                                  image.stack_base, &stack_info) ||
        !stack_info.writable || stack_info.executable ||
        (image.stack_base != (uintptr_t)INIT_STACK_BASE) ||
        (image.stack_top != (uintptr_t)INIT_STACK_TOP)) {
        init_test_fail("user-stack-mapped");
    }
    serial_write_string("  user-stack-mapped: PASS\n");

    if (!ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        init_test_fail("higher-half-supervisor-only");
    }
    serial_write_string("  higher-half-supervisor-only: PASS\n");

    serial_write_string("boring-init module size: ");
    serial_write_u64((uint64_t)module_size);
    serial_write_string(" bytes\nboring-init ELF entry: ");
    serial_write_hex_u64((uint64_t)image.entry);
    serial_write_string("\nboring-init process PID: ");
    serial_write_u64(process->pid);
    serial_write_string("\nboring-init user stack top: ");
    serial_write_hex_u64((uint64_t)image.stack_top);
    serial_write_string("\n");

    x86_64_interrupts_disable();
    if (!process_activate(process) ||
        !ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        init_test_fail("process-activate");
    }

    serial_write_string("Entering boring-init at CPL3.\n");
    x86_64_enter_ring3(image.entry,
                       image.stack_top,
                       (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                       (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                       0U);
}
