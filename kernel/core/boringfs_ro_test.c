#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/boot_dashboard.h>
#include <boring/framebuffer.h>
#include <boring/block_device.h>
#include <boring/boringfs.h>
#include <boring/boringfs_vfs.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_boot.h>
#include <boring/elf_loader.h>
#include <boring/process.h>
#include <boring/pmm.h>
#include <boring/ramfs.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/boringfs_ro_test.h>
#include <boring/syscall.h>
#include <boring/vfs.h>
#include <boring/vmm.h>
#include <boring/virtio_blk.h>

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
#define BORINGFS_FILESYSTEM_ID 230ULL
#if BORING_TEST_MODE == 15
#define BORINGFS_HELLO_PATH "/docs/hello.txt"
#define BORINGFS_ARCH_PATH "/docs/architecture.txt"
#define BORINGFS_TEST_ROOT 1
#else
#define BORINGFS_HELLO_PATH "/disk/docs/hello.txt"
#define BORINGFS_ARCH_PATH "/disk/docs/architecture.txt"
#define BORINGFS_TEST_ROOT 0
#endif

#if (BORING_TEST_MODE == 14) || (BORING_TEST_MODE == 15)
#define BORINGFS_TEST_WRITABLE 1
#else
#define BORINGFS_TEST_WRITABLE 0
#endif

static uint8_t boringfs_read_buffer[VFS_IO_MAX];

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

static void boringfs_ro_test_fail(const char *check) __attribute__((noreturn));
static void boringfs_ro_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string(BORINGFS_TEST_WRITABLE ?
                        "BoringFS writable acceptance FAILED: " :
                        "BoringFS read-only acceptance FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void boringfs_ro_test_pass(const char *check) {
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

void boringfs_ro_test_run(void) {
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
    struct boringfs_vfs *disk_boringfs = NULL;
    struct vfs_filesystem *disk_filesystem;
    const struct block_device *disk_device;
    struct boringfs_validation_error validation_error;
    struct vfs_path root = { NULL, NULL };
    struct vfs_path disk_mountpoint = { NULL, NULL };
    struct vfs_path disk_root = { NULL, NULL };
    struct vfs_path hello_path = { NULL, NULL };
    struct vfs_path architecture_path = { NULL, NULL };
    struct vfs_path denied_path = { NULL, NULL };
    struct vfs_path cwd_check = { NULL, NULL };
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    size_t transferred = 0U;
    struct ramfs_stats ramfs_stats;
    struct process_stats process_stats;
    struct process *init_process = NULL;

    serial_write_string("boring-shell launch:\n");

    if (!shell_modules_find(response, &modules)) {
        boringfs_ro_test_fail("boot-modules-found");
    }
    boringfs_ro_test_pass("boot-modules-found");
    if ((modules.init_bytes == NULL) || (modules.init_size == 0U)) {
        boringfs_ro_test_fail("init-module-found");
    }
    boringfs_ro_test_pass("init-module-found");
    if ((modules.shell_bytes == NULL) || (modules.shell_size == 0U)) {
        boringfs_ro_test_fail("shell-module-found");
    }
    boringfs_ro_test_pass("shell-module-found");

    if (!boring_elf_validate(modules.init_bytes, modules.init_size,
                             (uintptr_t)SHELL_STACK_BASE,
                             (size_t)BORING_ELF_PAGE_SIZE,
                             &init_validation) ||
        (init_validation.entry != (uintptr_t)SHELL_TEXT_VA)) {
        boringfs_ro_test_fail("init-elf64-image");
    }
    boringfs_ro_test_pass("init-elf64-image");
    if (!boring_elf_validate(modules.shell_bytes, modules.shell_size,
                             (uintptr_t)SHELL_STACK_BASE,
                             (size_t)BORING_ELF_PAGE_SIZE,
                             &shell_validation) ||
        (shell_validation.entry != (uintptr_t)SHELL_TEXT_VA)) {
        boringfs_ro_test_fail("shell-elf64-image");
    }
    boringfs_ro_test_pass("shell-elf64-image");

    if (!x86_64_enable_nx() || !x86_64_nx_enabled()) {
        boringfs_ro_test_fail("nx-enabled");
    }
    boringfs_ro_test_pass("nx-enabled");

    if (!process_init() || !syscall_init()) {
        boringfs_ro_test_fail("process-syscall-init");
    }
    boringfs_ro_test_pass("process-syscall-init");

    block_device_init();
    if (virtio_blk_init() != VIRTIO_BLK_RESULT_OK) {
        boringfs_ro_test_fail("virtio-init");
    }
    disk_device = block_device_find("vblk0");
    if ((disk_device == NULL) || (disk_device != virtio_blk_device()) ||
        (disk_device->logical_block_size != 512U)) {
        boringfs_ro_test_fail("vblk0-geometry");
    }
    boringfs_ro_test_pass("real-virtio-volume");

    if (BORINGFS_TEST_ROOT == 0) {
        if ((ramfs_create_filesystem(SHELL_ROOT_FILESYSTEM_ID, &root_ramfs) !=
             VFS_RESULT_OK) || (root_ramfs == NULL)) {
            boringfs_ro_test_fail("root-ramfs-create");
        }
        root_filesystem = ramfs_get_vfs(root_ramfs);
        if ((root_filesystem == NULL) ||
            (vfs_init(root_filesystem) != VFS_RESULT_OK) ||
            (vfs_get_root(&root) != VFS_RESULT_OK) ||
            (ramfs_get_stats(root_ramfs, &ramfs_stats) != VFS_RESULT_OK) ||
            (ramfs_stats.live_nodes != 1ULL) ||
            (ramfs_stats.live_directories != 1ULL) ||
            (ramfs_stats.live_files != 0ULL)) {
            boringfs_ro_test_fail("root-ramfs-vfs");
        }
        boringfs_ro_test_pass("root-ramfs-vfs");

        if (vfs_mkdir_at(&root, "disk", &disk_mountpoint) != VFS_RESULT_OK) {
            boringfs_ro_test_fail("disk-mountpoint");
        }
    }
    if (((BORINGFS_TEST_WRITABLE != 0) ?
         boringfs_vfs_create_writable(
             disk_device, BORINGFS_FILESYSTEM_ID, &disk_boringfs,
             &validation_error) :
         boringfs_vfs_create_readonly(
             disk_device, BORINGFS_FILESYSTEM_ID, &disk_boringfs,
             &validation_error)) != VFS_RESULT_OK) {
        serial_write_string("BoringFS mount rejected: ");
        serial_write_string(
            boringfs_validation_result_name(validation_error.code));
        serial_write_string("\n");
        x86_64_halt_forever();
    }
    disk_filesystem = boringfs_vfs_get_vfs(disk_boringfs);
    if ((disk_filesystem == NULL) ||
        ((BORINGFS_TEST_ROOT != 0) ?
         ((vfs_init(disk_filesystem) != VFS_RESULT_OK) ||
          (vfs_get_root(&root) != VFS_RESULT_OK)) :
         (vfs_mount_filesystem(disk_filesystem, &disk_mountpoint) !=
          VFS_RESULT_OK))) {
        boringfs_ro_test_fail("boringfs-mount");
    }
    serial_write_string("BoringFS over VirtIO:\n");
    boringfs_ro_test_pass("4096-to-512-mapping");
    boringfs_ro_test_pass("structural-validation");
    boringfs_ro_test_pass((BORINGFS_TEST_ROOT != 0) ?
                          "mount-at-root" : "mount-at-/disk");
    if (BORINGFS_TEST_ROOT != 0) {
        serial_write_string("BoringFS root mounted.\n");
    }

    if ((((BORINGFS_TEST_ROOT != 0) ?
          vfs_get_root(&disk_root) :
          vfs_resolve(&root, "/disk", &disk_root)) != VFS_RESULT_OK) ||
        (vfs_resolve(&root, BORINGFS_HELLO_PATH, &hello_path) !=
         VFS_RESULT_OK)) {
        boringfs_ro_test_fail("file-lookup");
    }
    serial_write_string("BoringFS file read:\n");
    boringfs_ro_test_pass("lookup");
    if ((vfs_handle_open(&hello_path, VFS_ACCESS_READ, &handle) !=
         VFS_RESULT_OK) ||
        (vfs_handle_read(&handle, boringfs_read_buffer,
                         sizeof("Hello from BoringFS on VirtIO.\n") - 1U,
                         &transferred) != VFS_RESULT_OK) ||
        (transferred != sizeof("Hello from BoringFS on VirtIO.\n") - 1U)) {
        boringfs_ro_test_fail("hello-read");
    }
    {
        static const char expected[] = "Hello from BoringFS on VirtIO.\n";
        size_t index;

        for (index = 0U; index < sizeof(expected) - 1U; ++index) {
            if (boringfs_read_buffer[index] != (uint8_t)expected[index]) {
                boringfs_ro_test_fail("hello-contents");
            }
        }
    }
    if (vfs_handle_close(&handle) != VFS_RESULT_OK) {
        boringfs_ro_test_fail("hello-close");
    }
    boringfs_ro_test_pass("contents");

    if ((vfs_resolve(&root, BORINGFS_ARCH_PATH, &architecture_path) !=
         VFS_RESULT_OK) ||
        (vfs_handle_open(&architecture_path, VFS_ACCESS_READ, &handle) !=
         VFS_RESULT_OK) ||
        (vfs_handle_read(&handle, boringfs_read_buffer, VFS_IO_MAX,
                         &transferred) != VFS_RESULT_OK) ||
        (transferred != VFS_IO_MAX)) {
        boringfs_ro_test_fail("multi-extent-first-block");
    }
    {
        size_t index;
        for (index = 0U; index < VFS_IO_MAX; ++index) {
            if (boringfs_read_buffer[index] !=
                (uint8_t)('A' + (char)(index % 26U))) {
                boringfs_ro_test_fail("multi-extent-first-pattern");
            }
        }
    }
    if ((vfs_handle_read(&handle, boringfs_read_buffer, VFS_IO_MAX,
                         &transferred) != VFS_RESULT_OK) ||
        (transferred != 104U)) {
        boringfs_ro_test_fail("multi-extent-second-block");
    }
    {
        size_t index;
        for (index = 0U; index < 104U; ++index) {
            if (boringfs_read_buffer[index] !=
                (uint8_t)('A' + (char)((VFS_IO_MAX + index) % 26U))) {
                boringfs_ro_test_fail("multi-extent-second-pattern");
            }
        }
    }
    if ((vfs_handle_read(&handle, boringfs_read_buffer, 1U, &transferred) !=
         VFS_RESULT_OK) || (transferred != 0U) ||
        (vfs_handle_close(&handle) != VFS_RESULT_OK)) {
        boringfs_ro_test_fail("multi-extent-eof");
    }
    boringfs_ro_test_pass("extent-backed-read");

    if (BORINGFS_TEST_WRITABLE != 0) {
        serial_write_string("BoringFS writable:\n");
        boringfs_ro_test_pass("synchronous-mutations-enabled");
    } else {
        serial_write_string("BoringFS read-only:\n");
        if (vfs_mkdir_at(&disk_root, "Denied", &denied_path) !=
            VFS_RESULT_ACCESS_DENIED) {
            boringfs_ro_test_fail("mkdir-denied");
        }
        boringfs_ro_test_pass("mkdir-denied");
        if (vfs_create_at(&disk_root, "denied.txt", &denied_path) !=
            VFS_RESULT_ACCESS_DENIED) {
            boringfs_ro_test_fail("create-denied");
        }
        boringfs_ro_test_pass("create-denied");
        if ((vfs_handle_open(&hello_path, VFS_ACCESS_WRITE, &handle) !=
             VFS_RESULT_OK) ||
            (vfs_handle_write(&handle, "x", 1U, &transferred) !=
             VFS_RESULT_ACCESS_DENIED) ||
            (vfs_handle_close(&handle) != VFS_RESULT_OK)) {
            boringfs_ro_test_fail("write-denied");
        }
        boringfs_ro_test_pass("write-denied");
        if (vfs_truncate_path(&hello_path, 0ULL) !=
            VFS_RESULT_ACCESS_DENIED) {
            boringfs_ro_test_fail("truncate-denied");
        }
        boringfs_ro_test_pass("truncate-denied");
        if (vfs_rmdir_at(&disk_root, "docs") != VFS_RESULT_ACCESS_DENIED) {
            boringfs_ro_test_fail("rmdir-denied");
        }
        boringfs_ro_test_pass("rmdir-denied");
    }

    if ((vfs_path_release(&architecture_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&hello_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&disk_root) != VFS_RESULT_OK) ||
        ((BORINGFS_TEST_ROOT == 0) &&
         (vfs_path_release(&disk_mountpoint) != VFS_RESULT_OK))) {
        boringfs_ro_test_fail("disk-path-release");
    }
    serial_write_string(BORINGFS_TEST_WRITABLE ?
                        "BoringFS writable mount ready.\n\n" :
                        "BoringFS read-only mount ready.\n\n");

#if BORING_TEST_MODE == 15
    {
        const struct boring_framebuffer *const surface = boring_framebuffer_get();
        struct pmm_stats memory_stats;

        if ((surface != NULL) && pmm_get_stats(&memory_stats)) {
            const struct boring_boot_dashboard_info dashboard_info = {
                .kernel_name = "BoringKernel",
                .kernel_version = "0.0.31-dev",
                .arch = "x86_64",
                .memory_bytes = memory_stats.usable_bytes,
                .root_fs = "BoringFS",
                .block_device = "VirtIO block",
                .pmm_online = true,
                .vmm_online = true,
                .irq_online = false,
                .ring3_available = true,
                .vfs_online = true,
                .boringfs_online = true
            };
            if (boring_boot_dashboard_render(surface, &dashboard_info)) {
                serial_write_string("boring-graphics: dashboard rendered\n\n");
            } else {
                serial_write_string("boring-graphics: dashboard skipped\n\n");
            }
        }
    }
#endif

    if (!process_create(&init_process) || (init_process == NULL) ||
        (init_process->pid != 1ULL)) {
        boringfs_ro_test_fail("init-process-pid1");
    }
    boringfs_ro_test_pass("init-process-pid1");

    if (!process_set_cwd(init_process, &root) ||
        !process_get_cwd(init_process, &cwd_check) ||
        !vfs_path_equal(&root, &cwd_check)) {
        boringfs_ro_test_fail("init-cwd-root");
    }
    if (vfs_path_release(&cwd_check) != VFS_RESULT_OK) {
        boringfs_ro_test_fail("init-cwd-root-release");
    }
    boringfs_ro_test_pass("init-cwd-root");

    if (!boring_elf_load(init_process, modules.init_bytes, modules.init_size,
                         (uintptr_t)SHELL_STACK_BASE,
                         (size_t)BORING_ELF_PAGE_SIZE, &init_image)) {
        boringfs_ro_test_fail("load-init-image");
    }
    boringfs_ro_test_pass("load-init-image");

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
        boringfs_ro_test_fail("init-memory-protections");
    }
    boringfs_ro_test_pass("init-memory-protections");

    if (!syscall_register_bootstrap_program(
            SHELL_PROGRAM_NAME, (size_t)SHELL_PROGRAM_NAME_LENGTH,
            modules.shell_bytes, modules.shell_size)) {
        boringfs_ro_test_fail("register-boring-shell");
    }
    boringfs_ro_test_pass("register-boring-shell");

    if (!process_get_stats(&process_stats) ||
        (process_stats.created_processes != 1ULL) ||
        (process_stats.active_processes != 1ULL) ||
        (process_stats.current_pid != 0ULL)) {
        boringfs_ro_test_fail("pre-launch-process-state");
    }
    boringfs_ro_test_pass("pre-launch-process-state");

    if (vfs_path_release(&root) != VFS_RESULT_OK) {
        boringfs_ro_test_fail("root-path-release");
    }
    boringfs_ro_test_pass("root-path-release");

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
        boringfs_ro_test_fail("init-process-activate");
    }

    serial_write_string("Entering boring-init at CPL3.\n");
    x86_64_enter_ring3(init_image.entry,
                       init_image.stack_top,
                       (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                       (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                       0U);
}
