#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_boot.h>
#include <boring/elf_loader.h>
#include <boring/process.h>
#include <boring/ramfs.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/shell_test.h>
#include <boring/syscall.h>
#include <boring/vfs.h>
#include <boring/vmm.h>

#define SHELL_INIT_MODULE_PATH "/boot/user/boring-init.elf"
#define SHELL_INIT_MODULE_STRING "boringos-boring-init"
#define SHELL_MODULE_PATH "/boot/user/boring-shell.elf"
#define SHELL_MODULE_STRING "boringos-boring-shell"
#define SHELL_PROGRAM_NAME "boring-shell"
#define SHELL_PROGRAM_NAME_LENGTH 12U
#define SHELL_TEXT_VA 0x0000000040000000ULL
#define SHELL_STACK_BASE 0x0000000040010000ULL
#define SHELL_STACK_TOP 0x0000000040011000ULL
#define SHELL_ROOT_FILESYSTEM_ID 200ULL

struct shell_boot_modules {
    const uint8_t *init_bytes;
    size_t init_size;
    const uint8_t *shell_bytes;
    size_t shell_size;
};

void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static void shell_test_fail(const char *check) __attribute__((noreturn));
static void shell_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("boring-shell acceptance FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void shell_test_pass(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": PASS\n");
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

static bool shell_module_bounds_valid(const struct boring_limine_file *module) {
    struct vmm_stats stats;
    uintptr_t address;
    uintptr_t last;

    if ((module == NULL) || !hhdm_pointer(module) ||
        !vmm_get_stats(&stats)) {
        return false;
    }
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
    return canonical_higher(last) &&
           ((uint64_t)address >= stats.hhdm_offset);
}

static bool shell_modules_find(
    const struct boring_limine_module_response *response,
    struct shell_boot_modules *modules_out) {
    bool init_found = false;
    bool shell_found = false;
    size_t index;

    if ((response == NULL) || (modules_out == NULL) ||
        (response->module_count != 2ULL) ||
        !hhdm_pointer(response->modules)) {
        return false;
    }

    modules_out->init_bytes = NULL;
    modules_out->init_size = 0U;
    modules_out->shell_bytes = NULL;
    modules_out->shell_size = 0U;

    for (index = 0U; index < 2U; ++index) {
        const struct boring_limine_file *const module = response->modules[index];

        if (!shell_module_bounds_valid(module)) {
            return false;
        }
        if (limine_string_equals(module->path, SHELL_INIT_MODULE_PATH) &&
            limine_string_equals(module->string, SHELL_INIT_MODULE_STRING)) {
            if (init_found) {
                return false;
            }
            modules_out->init_bytes = (const uint8_t *)module->address;
            modules_out->init_size = (size_t)module->size;
            init_found = true;
        } else if (limine_string_equals(module->path, SHELL_MODULE_PATH) &&
                   limine_string_equals(module->string,
                                        SHELL_MODULE_STRING)) {
            if (shell_found) {
                return false;
            }
            modules_out->shell_bytes = (const uint8_t *)module->address;
            modules_out->shell_size = (size_t)module->size;
            shell_found = true;
        } else {
            return false;
        }
    }

    return init_found && shell_found;
}

void shell_test_run(void) {
    const struct boring_limine_module_response *const response =
        elf_boot_module_response();
    struct shell_boot_modules modules;
    struct boring_elf_validation init_validation;
    struct boring_elf_validation shell_validation;
    struct boring_elf_image init_image;
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;
    struct ramfs *root_ramfs = NULL;
    struct vfs_filesystem *root_filesystem;
    struct vfs_path root = { NULL, NULL };
    struct vfs_path cwd_check = { NULL, NULL };
    struct ramfs_stats ramfs_stats;
    struct process_stats process_stats;
    struct process *init_process = NULL;

    serial_write_string("boring-shell launch:\n");

    if (!shell_modules_find(response, &modules)) {
        shell_test_fail("boot-modules-found");
    }
    shell_test_pass("boot-modules-found");
    if ((modules.init_bytes == NULL) || (modules.init_size == 0U)) {
        shell_test_fail("init-module-found");
    }
    shell_test_pass("init-module-found");
    if ((modules.shell_bytes == NULL) || (modules.shell_size == 0U)) {
        shell_test_fail("shell-module-found");
    }
    shell_test_pass("shell-module-found");

    if (!boring_elf_validate(modules.init_bytes, modules.init_size,
                             (uintptr_t)SHELL_STACK_BASE,
                             (size_t)BORING_ELF_PAGE_SIZE,
                             &init_validation) ||
        (init_validation.entry != (uintptr_t)SHELL_TEXT_VA)) {
        shell_test_fail("init-elf64-image");
    }
    shell_test_pass("init-elf64-image");
    if (!boring_elf_validate(modules.shell_bytes, modules.shell_size,
                             (uintptr_t)SHELL_STACK_BASE,
                             (size_t)BORING_ELF_PAGE_SIZE,
                             &shell_validation) ||
        (shell_validation.entry != (uintptr_t)SHELL_TEXT_VA)) {
        shell_test_fail("shell-elf64-image");
    }
    shell_test_pass("shell-elf64-image");

    if (!x86_64_enable_nx() || !x86_64_nx_enabled()) {
        shell_test_fail("nx-enabled");
    }
    shell_test_pass("nx-enabled");

    if (!process_init() || !syscall_init()) {
        shell_test_fail("process-syscall-init");
    }
    shell_test_pass("process-syscall-init");

    if ((ramfs_create_filesystem(SHELL_ROOT_FILESYSTEM_ID, &root_ramfs) !=
         VFS_RESULT_OK) || (root_ramfs == NULL)) {
        shell_test_fail("root-ramfs-create");
    }
    root_filesystem = ramfs_get_vfs(root_ramfs);
    if ((root_filesystem == NULL) ||
        (vfs_init(root_filesystem) != VFS_RESULT_OK) ||
        (vfs_get_root(&root) != VFS_RESULT_OK) ||
        (ramfs_get_stats(root_ramfs, &ramfs_stats) != VFS_RESULT_OK) ||
        (ramfs_stats.live_nodes != 1ULL) ||
        (ramfs_stats.live_directories != 1ULL) ||
        (ramfs_stats.live_files != 0ULL)) {
        shell_test_fail("root-ramfs-vfs");
    }
    shell_test_pass("root-ramfs-vfs");

    if (!process_create(&init_process) || (init_process == NULL) ||
        (init_process->pid != 1ULL)) {
        shell_test_fail("init-process-pid1");
    }
    shell_test_pass("init-process-pid1");

    if (!process_set_cwd(init_process, &root) ||
        !process_get_cwd(init_process, &cwd_check) ||
        !vfs_path_equal(&root, &cwd_check)) {
        shell_test_fail("init-cwd-root");
    }
    if (vfs_path_release(&cwd_check) != VFS_RESULT_OK) {
        shell_test_fail("init-cwd-root-release");
    }
    shell_test_pass("init-cwd-root");

    if (!boring_elf_load(init_process, modules.init_bytes, modules.init_size,
                         (uintptr_t)SHELL_STACK_BASE,
                         (size_t)BORING_ELF_PAGE_SIZE, &init_image)) {
        shell_test_fail("load-init-image");
    }
    shell_test_pass("load-init-image");

    if (!ring3_user_query_mapping(&init_process->address_space,
                                  init_image.entry, &entry_info) ||
        !entry_info.executable || entry_info.writable ||
        !ring3_user_query_mapping(&init_process->address_space,
                                  init_image.stack_base, &stack_info) ||
        !stack_info.writable || stack_info.executable ||
        (init_image.stack_base != (uintptr_t)SHELL_STACK_BASE) ||
        (init_image.stack_top != (uintptr_t)SHELL_STACK_TOP) ||
        !ring3_shared_higher_half_supervisor_only(
            &init_process->address_space)) {
        shell_test_fail("init-memory-protections");
    }
    shell_test_pass("init-memory-protections");

    if (!syscall_register_bootstrap_program(
            SHELL_PROGRAM_NAME, (size_t)SHELL_PROGRAM_NAME_LENGTH,
            modules.shell_bytes, modules.shell_size)) {
        shell_test_fail("register-boring-shell");
    }
    shell_test_pass("register-boring-shell");

    if (!process_get_stats(&process_stats) ||
        (process_stats.created_processes != 1ULL) ||
        (process_stats.active_processes != 1ULL) ||
        (process_stats.current_pid != 0ULL)) {
        shell_test_fail("pre-launch-process-state");
    }
    shell_test_pass("pre-launch-process-state");

    if (vfs_path_release(&root) != VFS_RESULT_OK) {
        shell_test_fail("root-path-release");
    }
    shell_test_pass("root-path-release");

    serial_write_string("boring-init module size: ");
    serial_write_u64((uint64_t)modules.init_size);
    serial_write_string(" bytes\nboring-shell module size: ");
    serial_write_u64((uint64_t)modules.shell_size);
    serial_write_string(" bytes\nboring-init ELF entry: ");
    serial_write_hex_u64((uint64_t)init_image.entry);
    serial_write_string("\nboring-init process PID: ");
    serial_write_u64(init_process->pid);
    serial_write_string("\nboring-init process root: ");
    serial_write_hex_u64(init_process->address_space.root_physical);
    serial_write_string("\nboring-init user stack top: ");
    serial_write_hex_u64((uint64_t)init_image.stack_top);
    serial_write_string("\n");

    x86_64_interrupts_disable();
    if (!process_activate(init_process) ||
        !ring3_shared_higher_half_supervisor_only(
            &init_process->address_space)) {
        shell_test_fail("init-process-activate");
    }

    serial_write_string("Entering boring-init at CPL3.\n");
    x86_64_enter_ring3(init_image.entry,
                       init_image.stack_top,
                       (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                       (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                       0U);
}
