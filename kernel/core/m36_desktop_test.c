#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/boot_protocol.h>
#include <boring/boringfs.h>
#include <boring/boringfs_vfs.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/display_test.h>
#include <boring/elf_loader.h>
#include <boring/framebuffer_user.h>
#include <boring/i8042.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/ipc_syscall.h>
#include <boring/irq.h>
#include <boring/m36_desktop_test.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/task.h>
#include <boring/user_memory.h>
#include <boring/vfs.h>
#include <boring/virtio_blk.h>
#include <boring/vmm.h>

#define M36_DESKTOP_BOOT_PROCESSES 2U
#define M36_DESKTOP_STACK_BASE 0x0000000040020000ULL
#define M36_DESKTOP_STACK_SIZE ((size_t)BORING_ELF_PAGE_SIZE)
#define M36_DESKTOP_BORINGFS_ID 236ULL

struct desktop_spec {
    const char *path;
    const char *string;
    const char *name;
};

struct desktop_state {
    struct process *process;
    struct boring_elf_image image;
    const uint8_t *module;
    size_t module_size;
    uint64_t task_id;
    bool image_loaded;
};

static const struct desktop_spec specs[M36_DESKTOP_BOOT_PROCESSES] = {
    {"/boot/user/boring-display.elf", "boringos-boring-display", "boring-display"},
    {"/boot/user/boringwm.elf", "boringos-boringwm", "boringwm"}
};
static struct desktop_state states[M36_DESKTOP_BOOT_PROCESSES];
static bool active;

void x86_64_enter_ring3(uintptr_t user_rip, uintptr_t user_rsp,
                        uint16_t user_cs, uint16_t user_ss,
                        uintptr_t result_address) __attribute__((noreturn));

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M36 desktop FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool text_equals(const char *left, const char *right) {
    size_t index = 0U;
    if ((left == NULL) || (right == NULL)) { return false; }
    while ((left[index] != '\0') && (right[index] != '\0') && (index < 96U)) {
        if (left[index] != right[index]) { return false; }
        ++index;
    }
    return (index < 96U) && (left[index] == '\0') && (right[index] == '\0');
}

static bool find_modules(const struct boring_limine_module_response *response) {
    bool found[M36_DESKTOP_BOOT_PROCESSES] = {false, false};
    size_t module_index;
    if ((response == NULL) ||
        (response->module_count != (uint64_t)M36_DESKTOP_BOOT_PROCESSES) ||
        (response->modules == NULL)) {
        return false;
    }
    for (module_index = 0U; module_index < M36_DESKTOP_BOOT_PROCESSES; ++module_index) {
        const struct boring_limine_file *module = response->modules[module_index];
        size_t spec_index;
        bool matched = false;
        if ((module == NULL) || (module->address == NULL) || (module->size == 0ULL) ||
            (module->size > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) ||
            (module->size > (uint64_t)SIZE_MAX)) {
            return false;
        }
        for (spec_index = 0U; spec_index < M36_DESKTOP_BOOT_PROCESSES; ++spec_index) {
            if (text_equals(module->path, specs[spec_index].path) &&
                text_equals(module->string, specs[spec_index].string)) {
                if (found[spec_index]) { return false; }
                states[spec_index].module = (const uint8_t *)module->address;
                states[spec_index].module_size = (size_t)module->size;
                found[spec_index] = true;
                matched = true;
                break;
            }
        }
        if (!matched) { return false; }
    }
    return found[0] && found[1];
}

static struct desktop_state *state_for(struct process *process) {
    size_t index;
    for (index = 0U; index < M36_DESKTOP_BOOT_PROCESSES; ++index) {
        if (states[index].process == process) { return &states[index]; }
    }
    return NULL;
}

bool boring_display_test_process_exit_prepare(struct process *process) {
    struct desktop_state *state;
    if (!active || (process == NULL)) { return false; }
    state = state_for(process);
    if ((state == NULL) || !state->image_loaded) { return false; }
    if (!boring_elf_unload(&state->image)) { fail("boot ELF unload"); }
    state->image_loaded = false;
    return true;
}

static void user_entry(void *argument) __attribute__((noreturn));
static void user_entry(void *argument) {
    struct desktop_state *state = (struct desktop_state *)argument;
    struct descriptor_stats descriptors;
    if ((state == NULL) || (state->process == NULL) || !state->image_loaded ||
        (process_current() != state->process) || !descriptor_get_stats(&descriptors)) {
        fail("boot process entry");
    }
    serial_write_string("m36-desktop: enter CPL3 pid ");
    serial_write_u64(state->process->pid);
    serial_write_string(" name ");
    serial_write_string(state->process->name);
    serial_write_string(" cr3 ");
    serial_write_hex_u64(state->process->address_space.root_physical);
    serial_write_string("\n");
    x86_64_enter_ring3(state->image.entry, state->image.stack_top,
                       descriptors.user_code_selector,
                       descriptors.user_data_selector, 0U);
}

static bool input_hardware_init(void) {
    struct i8042_state state = {false};
    x86_64_interrupts_disable();
    if (!boring_input_init() || !irq_init()) { return false; }
    (void)i8042_init(&state);
    if (!state.keyboard_online || !state.mouse_online ||
        !irq_unmask_input(state.keyboard_online, state.mouse_online)) {
        return false;
    }
    serial_write_string("m36-desktop: real PS/2 keyboard and mouse path online\n");
    return true;
}

static bool mount_root(struct vfs_path *root_out) {
    struct boringfs_vfs *disk_boringfs = NULL;
    struct vfs_filesystem *filesystem;
    const struct block_device *disk;
    struct boringfs_validation_error validation_error;
    block_device_init();
    if ((virtio_blk_init() != VIRTIO_BLK_RESULT_OK) ||
        ((disk = block_device_find("vblk0")) == NULL) ||
        (disk != virtio_blk_device()) || (disk->logical_block_size != 512U) ||
        (boringfs_vfs_create_readonly(disk, M36_DESKTOP_BORINGFS_ID,
                                      &disk_boringfs, &validation_error) != VFS_RESULT_OK)) {
        return false;
    }
    filesystem = boringfs_vfs_get_vfs(disk_boringfs);
    return (filesystem != NULL) && (vfs_init(filesystem) == VFS_RESULT_OK) &&
           (vfs_get_root(root_out) == VFS_RESULT_OK);
}

void m36_desktop_test_run(const struct boring_limine_module_response *modules) {
    struct vfs_path root = {NULL, NULL};
    struct boring_ipc_stats ipc;
    struct user_memory_global_stats memory;
    struct boring_input_stats input;
    struct boring_framebuffer_user_stats framebuffer;
    size_t index;

    active = false;
    for (index = 0U; index < M36_DESKTOP_BOOT_PROCESSES; ++index) {
        states[index] = (struct desktop_state){0};
    }
    serial_write_string("M36 graphical terminal desktop acceptance:\n");
    if (!find_modules(modules) || !x86_64_enable_nx() || !x86_64_nx_enabled() ||
        !process_init() || !task_init() || !syscall_init() ||
        !boring_ipc_system_init() || !input_hardware_init() || !mount_root(&root)) {
        fail("subsystem/root init");
    }
    boring_ipc_syscall_use_bootstrap_stack();
    serial_write_string("m36-desktop: real BoringFS root mounted\n");

    for (index = 0U; index < M36_DESKTOP_BOOT_PROCESSES; ++index) {
        struct process *process = NULL;
        struct ring3_user_mapping_info entry_info;
        if (!process_create(&process) || (process == NULL) ||
            !process_set_name(process, specs[index].name) ||
            !process_set_cwd_text(process, &root, "/") ||
            !boring_elf_load(process, states[index].module, states[index].module_size,
                             (uintptr_t)M36_DESKTOP_STACK_BASE,
                             M36_DESKTOP_STACK_SIZE, &states[index].image) ||
            !ring3_user_query_mapping(&process->address_space,
                                      states[index].image.entry, &entry_info) ||
            !entry_info.executable || entry_info.writable) {
            fail("boot process/image");
        }
        states[index].process = process;
        states[index].image_loaded = true;
    }
    if (vfs_path_release(&root) != VFS_RESULT_OK) { fail("root release"); }
    for (index = 0U; index < M36_DESKTOP_BOOT_PROCESSES; ++index) {
        if (!task_create_for_process(states[index].process, user_entry, &states[index],
                                     &states[index].task_id)) {
            fail("boot task create");
        }
    }
    active = true;
    serial_write_string("m36-desktop: display+wm scheduler tasks ready\n");

    for (;;) {
        bool all_finished = true;
        for (index = 0U; index < M36_DESKTOP_BOOT_PROCESSES; ++index) {
            if ((states[index].process != NULL) &&
                (states[index].process->state != PROCESS_FINISHED)) {
                all_finished = false;
            }
        }
        if (all_finished) { break; }
        task_yield();
    }
    active = false;
    boring_ipc_syscall_use_bootstrap_stack();

    if (!boring_ipc_get_stats(&ipc) || (ipc.live_services != 0U) ||
        (ipc.live_connections != 0U) || (ipc.queued_messages != 0U) ||
        (ipc.retained_buffer_attachments != 0U) ||
        !user_memory_get_global_stats(&memory) || (memory.active_objects != 0U) ||
        !boring_input_get_stats(&input) || input.owned ||
        !boring_framebuffer_user_get_stats(&framebuffer) || framebuffer.claimed) {
        fail("desktop global cleanup");
    }
    for (index = 0U; index < M36_DESKTOP_BOOT_PROCESSES; ++index) {
        if ((states[index].process == NULL) ||
            (states[index].process->state != PROCESS_FINISHED) ||
            !task_reap_finished_process(states[index].process) ||
            !process_destroy(states[index].process)) {
            fail("boot process cleanup");
        }
        states[index].process = NULL;
    }
    serial_write_string("m36-desktop: IPC/input/framebuffer/M32 cleanup complete\n");
    serial_write_string("M36 graphical terminal desktop acceptance passed.\n");
    x86_64_halt_forever();
}
