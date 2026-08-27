#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/boot_protocol.h>
#include <boring/boringfs.h>
#include <boring/boringfs_vfs.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_loader.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/m36_spawn_test.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/task.h>
#include <boring/vfs.h>
#include <boring/virtio_blk.h>

#define M36_PARENT_STACK_BASE 0x0000000040010000ULL
#define M36_PARENT_STACK_SIZE 4096U
#define M36_BORINGFS_ID 236ULL

struct m36_parent_state {
    struct process *process;
    struct boring_elf_image image;
    bool image_loaded;
};

void x86_64_enter_ring3_argv(uintptr_t user_rip,
                             uintptr_t user_rsp,
                             uint16_t user_cs,
                             uint16_t user_ss,
                             size_t argc,
                             uintptr_t argv) __attribute__((noreturn));

static void fail(const char *check) __attribute__((noreturn));
static void fail(const char *check) {
    serial_write_string("M36 spawn QEMU FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void parent_entry(void *argument) __attribute__((noreturn));
static void parent_entry(void *argument) {
    struct m36_parent_state *const state = (struct m36_parent_state *)argument;
    struct descriptor_stats descriptors;

    if ((state == NULL) || (state->process == NULL) || !state->image_loaded ||
        (process_current() != state->process) ||
        !descriptor_get_stats(&descriptors)) {
        fail("scheduler parent entry");
    }
    serial_write_string("m36-spawn-test: Ring3 parent pid ");
    serial_write_u64(state->process->pid);
    serial_write_string(" cr3 ");
    serial_write_hex_u64(state->process->address_space.root_physical);
    serial_write_string("\n");
    x86_64_enter_ring3_argv(state->image.entry, state->image.stack_top,
                            descriptors.user_code_selector,
                            descriptors.user_data_selector, 0U, 0U);
}

void m36_spawn_test_run(const struct boring_limine_module_response *modules) {
    static struct m36_parent_state parent;
    struct boringfs_vfs *disk_boringfs = NULL;
    struct vfs_filesystem *disk_filesystem;
    const struct block_device *disk;
    struct boringfs_validation_error validation_error;
    struct vfs_path root = { NULL, NULL };
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;
    const struct boring_limine_file *module;
    struct process *process = NULL;
    uint64_t task_id = 0ULL;

    serial_write_string("M36 Scheduler + Ring3 + BoringFS + PTY + SPAWN acceptance:\n");
    if ((modules == NULL) || (modules->module_count != 1ULL) ||
        (modules->modules == NULL) || (modules->modules[0] == NULL)) {
        fail("parent boot module");
    }
    module = modules->modules[0];
    if ((module->address == NULL) || (module->size == 0ULL) ||
        (module->size > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) ||
        (module->size > (uint64_t)SIZE_MAX)) {
        fail("parent module bounds");
    }
    if (!x86_64_enable_nx() || !x86_64_nx_enabled() ||
        !process_init() || !task_init() || !syscall_init() ||
        !boring_ipc_system_init() || !boring_input_init()) {
        fail("subsystem init");
    }
    serial_write_string("m36-spawn-test: scheduler active\n");

    block_device_init();
    if ((virtio_blk_init() != VIRTIO_BLK_RESULT_OK) ||
        ((disk = block_device_find("vblk0")) == NULL) ||
        (disk != virtio_blk_device()) || (disk->logical_block_size != 512U)) {
        fail("real VirtIO block device");
    }
    if (boringfs_vfs_create_readonly(disk, M36_BORINGFS_ID, &disk_boringfs,
                                     &validation_error) != VFS_RESULT_OK) {
        fail("BoringFS create");
    }
    disk_filesystem = boringfs_vfs_get_vfs(disk_boringfs);
    if ((disk_filesystem == NULL) ||
        (vfs_init(disk_filesystem) != VFS_RESULT_OK) ||
        (vfs_get_root(&root) != VFS_RESULT_OK)) {
        fail("BoringFS root mount");
    }
    serial_write_string("m36-spawn-test: real BoringFS root mounted\n");

    if (!process_create(&process) || (process == NULL) ||
        !process_set_name(process, "m36-parent") ||
        !process_set_cwd_text(process, &root, "/") ||
        !boring_elf_load(process, (const uint8_t *)module->address,
                         (size_t)module->size,
                         (uintptr_t)M36_PARENT_STACK_BASE,
                         (size_t)M36_PARENT_STACK_SIZE, &parent.image)) {
        fail("parent process/ELF");
    }
    parent.process = process;
    parent.image_loaded = true;
    if ((process->address_space.root_physical ==
         process_bootstrap()->address_space.root_physical) ||
        !ring3_user_query_mapping(&process->address_space, parent.image.entry,
                                  &entry_info) || !entry_info.executable ||
        entry_info.writable ||
        !ring3_user_query_mapping(&process->address_space,
                                  parent.image.stack_base, &stack_info) ||
        !stack_info.writable || stack_info.executable ||
        !ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        fail("parent address space");
    }
    if (vfs_path_release(&root) != VFS_RESULT_OK) {
        fail("root reference release");
    }
    if (!task_create_for_process(process, parent_entry, &parent, &task_id)) {
        fail("scheduler parent task");
    }
    serial_write_string("m36-spawn-test: scheduler task ");
    serial_write_u64(task_id);
    serial_write_string(" ready\n");
    task_yield();
    fail("scheduler parent unexpectedly returned");
}
