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
#include <boring/m38_desktop_test.h>
#include <boring/process.h>
#include <boring/pty.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/task.h>
#include <boring/user_memory.h>
#include <boring/vfs.h>
#include <boring/virtio_blk.h>
#include <boring/vmm.h>

#define M38_INIT_MODULE_PATH "/boot/user/boring-init.elf"
#define M38_INIT_MODULE_STRING "boringos-boring-init"
#define M38_STACK_BASE 0x0000000040020000ULL
#define M38_STACK_SIZE ((size_t)BORING_ELF_PAGE_SIZE)
#define M38_BORINGFS_ID 238ULL

struct m38_init_state {
    struct process *process;
    struct boring_elf_image image;
    const uint8_t *module;
    size_t module_size;
    uint64_t task_id;
    bool image_loaded;
};

static struct m38_init_state init_state;
static bool active;

void x86_64_enter_ring3(uintptr_t user_rip, uintptr_t user_rsp,
                        uint16_t user_cs, uint16_t user_ss,
                        uintptr_t result_address) __attribute__((noreturn));

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M38 desktop FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool text_equals(const char *left, const char *right) {
    size_t index = 0U;

    if ((left == NULL) || (right == NULL)) {
        return false;
    }
    while ((left[index] != '\0') && (right[index] != '\0') &&
           (index < 96U)) {
        if (left[index] != right[index]) {
            return false;
        }
        ++index;
    }
    return (index < 96U) && (left[index] == '\0') &&
           (right[index] == '\0');
}

static bool find_init_module(
    const struct boring_limine_module_response *response) {
    const struct boring_limine_file *module;

    if ((response == NULL) || (response->module_count != 1ULL) ||
        (response->modules == NULL) ||
        ((module = response->modules[0]) == NULL) ||
        (module->address == NULL) || (module->size == 0ULL) ||
        (module->size > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) ||
        (module->size > (uint64_t)SIZE_MAX) ||
        !text_equals(module->path, M38_INIT_MODULE_PATH) ||
        !text_equals(module->string, M38_INIT_MODULE_STRING)) {
        return false;
    }
    init_state.module = (const uint8_t *)module->address;
    init_state.module_size = (size_t)module->size;
    return true;
}

bool boring_display_test_process_exit_prepare(struct process *process) {
    if (!active || (process == NULL) || (process != init_state.process) ||
        !init_state.image_loaded) {
        return false;
    }
    if (!boring_elf_unload(&init_state.image)) {
        fail("init ELF unload");
    }
    init_state.image_loaded = false;
    return true;
}

static void init_entry(void *argument) __attribute__((noreturn));
static void init_entry(void *argument) {
    struct m38_init_state *state = (struct m38_init_state *)argument;
    struct descriptor_stats descriptors;

    if ((state == NULL) || (state->process == NULL) || !state->image_loaded ||
        (process_current() != state->process) ||
        !descriptor_get_stats(&descriptors)) {
        fail("init process entry");
    }
    serial_write_string("m38-desktop: enter CPL3 pid ");
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
    if (!boring_input_init() || !irq_init()) {
        return false;
    }
    (void)i8042_init(&state);
    if (!state.keyboard_online || !state.mouse_online ||
        !irq_unmask_input(state.keyboard_online, state.mouse_online)) {
        return false;
    }
    serial_write_string(
        "m38-desktop: real PS/2 keyboard and mouse path online\n");
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
#if defined(BORING_M39_EDIT_ACCEPTANCE)
        (boringfs_vfs_create_writable(disk, M38_BORINGFS_ID,
#else
        (boringfs_vfs_create_readonly(disk, M38_BORINGFS_ID,
#endif
                                      &disk_boringfs,
                                      &validation_error) != VFS_RESULT_OK)) {
        return false;
    }
    filesystem = boringfs_vfs_get_vfs(disk_boringfs);
    return (filesystem != NULL) && (vfs_init(filesystem) == VFS_RESULT_OK) &&
           (vfs_get_root(root_out) == VFS_RESULT_OK);
}

static void report_drain(const struct boring_ipc_stats *ipc,
                         const struct user_memory_global_stats *memory,
                         const struct boring_input_stats *input,
                         const struct boring_framebuffer_user_stats *framebuffer,
                         const struct pty_stats *pty,
                         const struct process_stats *processes,
                         const struct task_stats *tasks) {
    serial_write_string("m38-desktop: drain ipc services/connections/queued/attachments=");
    serial_write_u64((uint64_t)ipc->live_services);
    serial_write_string("/");
    serial_write_u64((uint64_t)ipc->live_connections);
    serial_write_string("/");
    serial_write_u64((uint64_t)ipc->queued_messages);
    serial_write_string("/");
    serial_write_u64((uint64_t)ipc->retained_buffer_attachments);
    serial_write_string(" memory_objects=");
    serial_write_u64((uint64_t)memory->active_objects);
    serial_write_string(" input/fb=");
    serial_write_u64(input->owned ? 1ULL : 0ULL);
    serial_write_string("/");
    serial_write_u64(framebuffer->claimed ? 1ULL : 0ULL);
    serial_write_string("\n");

    serial_write_string("m38-desktop: drain pty pairs/refs/waiters/bytes=");
    serial_write_u64((uint64_t)pty->active_pairs);
    serial_write_string("/");
    serial_write_u64(pty->references);
    serial_write_string("/");
    serial_write_u64((uint64_t)pty->read_waiters);
    serial_write_string("/");
    serial_write_u64((uint64_t)pty->queued_bytes);
    serial_write_string("\n");

    serial_write_string("m38-desktop: drain proc active/created/finished/current=");
    serial_write_u64(processes->active_processes);
    serial_write_string("/");
    serial_write_u64(processes->created_processes);
    serial_write_string("/");
    serial_write_u64(processes->finished_processes);
    serial_write_string("/");
    serial_write_u64(processes->current_pid);
    serial_write_string(" task active/created/finished/current/pid/resume/fault=");
    serial_write_u64(tasks->active_tasks);
    serial_write_string("/");
    serial_write_u64(tasks->created_tasks);
    serial_write_string("/");
    serial_write_u64(tasks->finished_tasks);
    serial_write_string("/");
    serial_write_u64(tasks->current_task_id);
    serial_write_string("/");
    serial_write_u64(tasks->current_process_pid);
    serial_write_string("/");
    serial_write_u64(tasks->finished_resume_count);
    serial_write_string("/");
    serial_write_u64(tasks->scheduler_fault_count);
    serial_write_string("\n");
}

void m38_desktop_test_finish_from_pid1(void) {
    struct boring_ipc_stats ipc;
    struct user_memory_global_stats memory;
    struct boring_input_stats input;
    struct boring_framebuffer_user_stats framebuffer;
    struct pty_stats pty;
    struct process_stats processes;
    struct task_stats tasks;

    if (!active || (init_state.process == NULL) ||
        (process_current() != init_state.process) ||
        (init_state.process->pid != 1ULL) ||
        (init_state.process->state != PROCESS_ALIVE) ||
        !init_state.image_loaded ||
        !boring_ipc_get_stats(&ipc) ||
        !user_memory_get_global_stats(&memory) ||
        !boring_input_get_stats(&input) ||
        !boring_framebuffer_user_get_stats(&framebuffer) ||
        !pty_get_stats(&pty) || !process_get_stats(&processes) ||
        !task_get_stats(&tasks)) {
        fail("resource stats");
    }

    report_drain(&ipc, &memory, &input, &framebuffer, &pty,
                 &processes, &tasks);
    if ((ipc.live_services != 0U) || (ipc.live_connections != 0U) ||
        (ipc.queued_messages != 0U) ||
        (ipc.retained_buffer_attachments != 0U) ||
        (memory.active_objects != 0U) || input.owned || framebuffer.claimed ||
        (pty.active_pairs != 0U) || (pty.references != 0ULL) ||
        (pty.read_waiters != 0U) || (pty.queued_bytes != 0U) ||
        (processes.active_processes != 1ULL) ||
        (processes.created_processes != processes.finished_processes + 1ULL) ||
        (processes.current_pid != 1ULL) ||
        (tasks.active_tasks != 1ULL) ||
        (tasks.created_tasks != tasks.finished_tasks + 1ULL) ||
        (tasks.current_task_id != init_state.task_id) ||
        (tasks.current_process_pid != 1ULL) ||
        (tasks.finished_resume_count != 0ULL) ||
        (tasks.scheduler_fault_count != 0ULL)) {
        fail("desktop drain accounting in PID 1");
    }
    serial_write_string(
        "m38-desktop: IPC/input/framebuffer/M32/PTY resources drained\n");
    serial_write_string(
        "m38-desktop: all desktop tasks/processes reaped; PID 1 remains\n");
    serial_write_string(
        "M38 native desktop session supervision resource acceptance passed.\n");
    x86_64_halt_forever();
}

void m38_desktop_test_run(
    const struct boring_limine_module_response *modules) {
    struct vfs_path root = {NULL, NULL};
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;

    init_state = (struct m38_init_state){0};
    active = false;
    serial_write_string("M38 native desktop session supervision acceptance:\n");
    if (!find_init_module(modules) || !x86_64_enable_nx() ||
        !x86_64_nx_enabled() || !process_init() || !task_init() ||
        !syscall_init() || !boring_ipc_system_init()) {
        fail("subsystem init");
    }
    if (!input_hardware_init()) {
        fail("input hardware");
    }
    if (!mount_root(&root)) {
        fail("persistent root");
    }
    boring_ipc_syscall_use_bootstrap_stack();
    serial_write_string("m38-desktop: real BoringFS root mounted\n");

    if (!process_create(&init_state.process) || (init_state.process == NULL) ||
        (init_state.process->pid != 1ULL) ||
        !process_set_name(init_state.process, "boring-init") ||
        !process_set_cwd_text(init_state.process, &root, "/") ||
        !boring_elf_load(init_state.process, init_state.module,
                         init_state.module_size, (uintptr_t)M38_STACK_BASE,
                         M38_STACK_SIZE, &init_state.image) ||
        !ring3_user_query_mapping(&init_state.process->address_space,
                                  init_state.image.entry, &entry_info) ||
        !entry_info.executable || entry_info.writable ||
        !ring3_user_query_mapping(&init_state.process->address_space,
                                  init_state.image.stack_base, &stack_info) ||
        !stack_info.writable || stack_info.executable ||
        !ring3_shared_higher_half_supervisor_only(
            &init_state.process->address_space)) {
        fail("init process/image");
    }
    init_state.image_loaded = true;
    if (vfs_path_release(&root) != VFS_RESULT_OK) {
        fail("root release");
    }
    if (!task_create_for_process(init_state.process, init_entry, &init_state,
                                 &init_state.task_id)) {
        fail("init task create");
    }
    active = true;
    serial_write_string(
        "m38-desktop: PID 1 scheduler task ready; desktop children come from BoringFS\n");

    task_yield();
    fail("PID 1 unexpectedly returned to bootstrap");
}
