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
#include <boring/m37_desktop_test.h>
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
#if defined(BORING_M57_AHCI_ROOT)
#include <boring/ahci.h>
#include <boring/ahci_block.h>
#endif
#if defined(BORING_M54_USB_ONLY_DESKTOP)
#include <boring/xhci.h>
#include <boring/timer.h>
#endif

#define M37_INIT_MODULE_PATH "/boot/user/boring-init.elf"
#define M37_INIT_MODULE_STRING "boringos-boring-init"
#define M37_STACK_BASE 0x0000000040020000ULL
#define M37_STACK_SIZE ((size_t)BORING_ELF_PAGE_SIZE)
#define M37_BORINGFS_ID 237ULL

struct m37_init_state {
    struct process *process;
    struct boring_elf_image image;
    const uint8_t *module;
    size_t module_size;
    uint64_t task_id;
    bool image_loaded;
};

static struct m37_init_state init_state;
static bool active;

void x86_64_enter_ring3(uintptr_t user_rip, uintptr_t user_rsp,
                        uint16_t user_cs, uint16_t user_ss,
                        uintptr_t result_address) __attribute__((noreturn));
void m37_desktop_test_finish_from_pid1(void) __attribute__((noreturn));

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M37 desktop FAILED: ");
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
        (response->modules == NULL) || ((module = response->modules[0]) == NULL) ||
        (module->address == NULL) || (module->size == 0ULL) ||
        (module->size > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) ||
        (module->size > (uint64_t)SIZE_MAX) ||
        !text_equals(module->path, M37_INIT_MODULE_PATH) ||
        !text_equals(module->string, M37_INIT_MODULE_STRING)) {
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
    struct m37_init_state *state = (struct m37_init_state *)argument;
    struct descriptor_stats descriptors;

    if ((state == NULL) || (state->process == NULL) || !state->image_loaded ||
        (process_current() != state->process) ||
        !descriptor_get_stats(&descriptors)) {
        fail("init process entry");
    }
    serial_write_string("m37-desktop: enter CPL3 pid ");
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

#if defined(BORING_M54_USB_ONLY_DESKTOP)
static bool m54_hid_protocols_ready(const struct xhci_state *state) {
    bool keyboard = false;
    bool pointer = false;
    uint8_t device_index;

    if ((state == NULL) || !state->controller_running ||
        (state->addressed_count == 0U)) {
        return false;
    }
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device = &state->addressed[device_index];
        uint8_t endpoint_index;
        if (!device->device_configured || !device->hid_endpoint_ready) {
            continue;
        }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const uint8_t protocol =
                device->hid_configuration.endpoints[endpoint_index].protocol;
            if (protocol == 1U) {
                keyboard = true;
            } else if ((protocol == 0U) || (protocol == 2U)) {
                pointer = true;
            }
        }
    }
    return keyboard && pointer;
}

static bool m54_usb_runtime_evidence(void) {
    const struct xhci_state *state = xhci_get_state();
    uint64_t completions = 0ULL;
    uint64_t decoded = 0ULL;
    uint64_t key_presses = 0ULL;
    uint64_t key_releases = 0ULL;
    uint64_t pointer_reports = 0ULL;
    bool keyboard = false;
    bool pointer = false;
    bool pointer_buttons_released = true;
    uint8_t device_index;

    if (!m54_hid_protocols_ready(state)) {
        return false;
    }
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device = &state->addressed[device_index];
        uint8_t endpoint_index;
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            const uint8_t protocol =
                device->hid_configuration.endpoints[endpoint_index].protocol;
            completions += runtime->completed_transfers;
            decoded += runtime->decoded_reports;
            if (protocol == 1U) {
                keyboard = true;
                key_presses += runtime->key_presses;
                key_releases += runtime->key_releases;
            } else if ((protocol == 0U) || (protocol == 2U)) {
                pointer = true;
                pointer_reports += runtime->pointer_reports;
                if (runtime->last_pointer_buttons != 0U) {
                    pointer_buttons_released = false;
                }
            }
        }
    }
    serial_write_string(
        "m54-desktop: USB HID completions/decoded/key-down/key-up/pointer=");
    serial_write_u64(completions);
    serial_write_string("/"); serial_write_u64(decoded);
    serial_write_string("/"); serial_write_u64(key_presses);
    serial_write_string("/"); serial_write_u64(key_releases);
    serial_write_string("/"); serial_write_u64(pointer_reports);
    serial_write_string("\n");
    return keyboard && pointer && pointer_buttons_released &&
           (completions == decoded) && (decoded >= 8ULL) &&
           (key_presses != 0ULL) && (key_releases != 0ULL) &&
           (pointer_reports >= 4ULL);
}

#ifndef BORING_M54_HID_READY_POLICY
#define BORING_M54_HID_READY_POLICY(state) m54_hid_protocols_ready(state)
#endif
#ifndef BORING_M54_USB_RUNTIME_GATE
#define BORING_M54_USB_RUNTIME_GATE(evidence) (evidence)
#endif
#endif

static bool input_hardware_init(void) {
#if defined(BORING_M54_USB_ONLY_DESKTOP)
    struct xhci_state state = {0};

    x86_64_interrupts_disable();
    if (!boring_input_init() || !irq_init() || !timer_init(100U) ||
        !xhci_init(&state) ||
        !xhci_address_connected(&state) ||
        !xhci_discover_descriptors(&state) ||
        !xhci_configure_hid_devices(&state) ||
        !(m54_hid_protocols_ready(&state),
          BORING_M54_HID_READY_POLICY(&state))) {
        return false;
    }
    serial_write_string(
        "m54-desktop: q35 i8042-free xHCI USB keyboard/tablet path online\n");
    return true;
#else
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
        "m37-desktop: real PS/2 keyboard and mouse path online\n");
    return true;
#endif
}

static bool mount_root(struct vfs_path *root_out) {
    struct boringfs_vfs *disk_boringfs = NULL;
    struct vfs_filesystem *filesystem;
    const struct block_device *disk;
    struct boringfs_validation_error validation_error;

    block_device_init();
#if defined(BORING_M57_AHCI_ROOT)
    {
        struct ahci_state controller;
        struct ahci_block_stats stats;
        if (!ahci_init(&controller) ||
            (ahci_block_init(&controller) != AHCI_BLOCK_RESULT_OK) ||
            ((disk = block_device_find(AHCI_BLOCK_DEVICE_NAME)) == NULL) ||
            (disk != ahci_block_device()) || disk->read_only ||
            (disk->logical_block_size != 512U) ||
            !ahci_block_get_stats(&stats) || !stats.identify_complete ||
            (boringfs_vfs_create_writable(disk, M37_BORINGFS_ID,
                                          &disk_boringfs,
                                          &validation_error) != VFS_RESULT_OK)) {
            return false;
        }
        serial_write_string(
            "m57-desktop: writable BoringFS root mounted through AHCI sata0\n");
    }
#else
    if ((virtio_blk_init() != VIRTIO_BLK_RESULT_OK) ||
        ((disk = block_device_find("vblk0")) == NULL) ||
        (disk != virtio_blk_device()) || (disk->logical_block_size != 512U) ||
        (boringfs_vfs_create_readonly(disk, M37_BORINGFS_ID,
                                      &disk_boringfs,
                                      &validation_error) != VFS_RESULT_OK)) {
        return false;
    }
#endif
    filesystem = boringfs_vfs_get_vfs(disk_boringfs);
    return (filesystem != NULL) && (vfs_init(filesystem) == VFS_RESULT_OK) &&
           (vfs_get_root(root_out) == VFS_RESULT_OK);
}

static void report_drain_stats(bool ipc_ok, const struct boring_ipc_stats *ipc,
                               bool memory_ok,
                               const struct user_memory_global_stats *memory,
                               bool input_ok, const struct boring_input_stats *input,
                               bool framebuffer_ok,
                               const struct boring_framebuffer_user_stats *framebuffer,
                               bool pty_ok, const struct pty_stats *pty,
                               bool processes_ok,
                               const struct process_stats *processes,
                               bool tasks_ok, const struct task_stats *tasks) {
    serial_write_string("m37-desktop: drain diagnostics state active=");
    serial_write_u64(active ? 1ULL : 0ULL);
    serial_write_string(" pid1_ptr=");
    serial_write_u64((init_state.process != NULL) ? 1ULL : 0ULL);
    serial_write_string(" current_pid=");
    serial_write_u64((process_current() != NULL) ? process_current()->pid : UINT64_MAX);
    serial_write_string(" init_task=");
    serial_write_u64(init_state.task_id);
    serial_write_string(" image_loaded=");
    serial_write_u64(init_state.image_loaded ? 1ULL : 0ULL);
    serial_write_string("\n");

    serial_write_string("m37-desktop: drain diagnostics getters ipc/mem/input/fb/pty/proc/task=");
    serial_write_u64(ipc_ok ? 1ULL : 0ULL);
    serial_write_string("/");
    serial_write_u64(memory_ok ? 1ULL : 0ULL);
    serial_write_string("/");
    serial_write_u64(input_ok ? 1ULL : 0ULL);
    serial_write_string("/");
    serial_write_u64(framebuffer_ok ? 1ULL : 0ULL);
    serial_write_string("/");
    serial_write_u64(pty_ok ? 1ULL : 0ULL);
    serial_write_string("/");
    serial_write_u64(processes_ok ? 1ULL : 0ULL);
    serial_write_string("/");
    serial_write_u64(tasks_ok ? 1ULL : 0ULL);
    serial_write_string("\n");

    if (ipc_ok) {
        serial_write_string("m37-desktop: drain diagnostics ipc services/connections/queued/attachments=");
        serial_write_u64((uint64_t)ipc->live_services);
        serial_write_string("/");
        serial_write_u64((uint64_t)ipc->live_connections);
        serial_write_string("/");
        serial_write_u64((uint64_t)ipc->queued_messages);
        serial_write_string("/");
        serial_write_u64((uint64_t)ipc->retained_buffer_attachments);
        serial_write_string("\n");
    }
    if (memory_ok) {
        serial_write_string("m37-desktop: drain diagnostics memory active_objects=");
        serial_write_u64((uint64_t)memory->active_objects);
        serial_write_string("\n");
    }
    if (input_ok && framebuffer_ok) {
        serial_write_string("m37-desktop: drain diagnostics input_owned/fb_claimed=");
        serial_write_u64(input->owned ? 1ULL : 0ULL);
        serial_write_string("/");
        serial_write_u64(framebuffer->claimed ? 1ULL : 0ULL);
        serial_write_string("\n");
    }
    if (pty_ok) {
        serial_write_string("m37-desktop: drain diagnostics pty pairs/refs/waiters/bytes=");
        serial_write_u64((uint64_t)pty->active_pairs);
        serial_write_string("/");
        serial_write_u64(pty->references);
        serial_write_string("/");
        serial_write_u64((uint64_t)pty->read_waiters);
        serial_write_string("/");
        serial_write_u64((uint64_t)pty->queued_bytes);
        serial_write_string("\n");
    }
    if (processes_ok) {
        serial_write_string("m37-desktop: drain diagnostics proc active/created/finished/current=");
        serial_write_u64(processes->active_processes);
        serial_write_string("/");
        serial_write_u64(processes->created_processes);
        serial_write_string("/");
        serial_write_u64(processes->finished_processes);
        serial_write_string("/");
        serial_write_u64(processes->current_pid);
        serial_write_string("\n");
    }
    if (tasks_ok) {
        serial_write_string("m37-desktop: drain diagnostics task active/created/finished/current/pid/resume/fault=");
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
}

void m37_desktop_test_finish_from_pid1(void) {
    struct boring_ipc_stats ipc;
    struct user_memory_global_stats memory;
    struct boring_input_stats input;
    struct boring_framebuffer_user_stats framebuffer;
    struct pty_stats pty;
    struct process_stats processes;
    struct task_stats tasks;
    const bool ipc_ok = boring_ipc_get_stats(&ipc);
    const bool memory_ok = user_memory_get_global_stats(&memory);
    const bool input_ok = boring_input_get_stats(&input);
    const bool framebuffer_ok = boring_framebuffer_user_get_stats(&framebuffer);
    const bool pty_ok = pty_get_stats(&pty);
    const bool processes_ok = process_get_stats(&processes);
    const bool tasks_ok = task_get_stats(&tasks);

#if defined(BORING_M54_USB_ONLY_DESKTOP)
    const bool m54_usb_evidence = m54_usb_runtime_evidence();
    const bool m54_usb_ok = BORING_M54_USB_RUNTIME_GATE(m54_usb_evidence);
#else
    const bool m54_usb_ok = true;
#endif

    if (!m54_usb_ok || !active || (init_state.process == NULL) ||
        (process_current() != init_state.process) ||
        (init_state.process->pid != 1ULL) ||
        (init_state.process->state != PROCESS_ALIVE) ||
        !init_state.image_loaded ||
        !ipc_ok || (ipc.live_services != 0U) ||
        (ipc.live_connections != 0U) || (ipc.queued_messages != 0U) ||
        (ipc.retained_buffer_attachments != 0U) ||
        !memory_ok || (memory.active_objects != 0U) ||
        !input_ok || input.owned ||
        !framebuffer_ok || framebuffer.claimed ||
        !pty_ok || (pty.active_pairs != 0U) ||
        (pty.references != 0ULL) || (pty.read_waiters != 0U) ||
        (pty.queued_bytes != 0U) ||
        !processes_ok || (processes.active_processes != 1ULL) ||
        (processes.created_processes != processes.finished_processes + 1ULL) ||
        (processes.current_pid != 1ULL) ||
        !tasks_ok || (tasks.active_tasks != 1ULL) ||
        (tasks.created_tasks != tasks.finished_tasks + 1ULL) ||
        (tasks.current_task_id != init_state.task_id) ||
        (tasks.current_process_pid != 1ULL) ||
        (tasks.finished_resume_count != 0ULL) ||
        (tasks.scheduler_fault_count != 0ULL)) {
        report_drain_stats(ipc_ok, &ipc, memory_ok, &memory, input_ok, &input,
                           framebuffer_ok, &framebuffer, pty_ok, &pty,
                           processes_ok, &processes, tasks_ok, &tasks);
        fail("desktop drain accounting in PID 1");
    }
    serial_write_string(
        "m37-desktop: IPC/input/framebuffer/M32/PTY desktop resources drained\n");
    serial_write_string(
        "m37-desktop: all spawned desktop tasks/processes reaped; PID 1 remains\n");
#if defined(BORING_M57_AHCI_ROOT)
    {
        struct ahci_block_stats ahci_stats;
        if (!ahci_block_get_stats(&ahci_stats) ||
            (ahci_stats.writes_completed == 0ULL) ||
            !ahci_stats.write_cache_enabled ||
            (ahci_stats.flushes_completed == 0ULL)) {
            fail("M57 AHCI write/flush accounting");
        }
        serial_write_string("m57-desktop: AHCI writes/flushes=");
        serial_write_u64(ahci_stats.writes_completed);
        serial_write_string("/");
        serial_write_u64(ahci_stats.flushes_completed);
        serial_write_string("\nM57 writable AHCI persistent-root desktop passed.\n");
    }
#endif
#if defined(BORING_M54_USB_ONLY_DESKTOP)
    if (m54_usb_evidence) {
        serial_write_string(
            "M54 USB-only graphical desktop acceptance passed.\n");
    }
#endif
    serial_write_string(
        "M37 native desktop session startup acceptance passed.\n");
    x86_64_halt_forever();
}

void m37_desktop_test_run(
    const struct boring_limine_module_response *modules) {
    struct vfs_path root = {NULL, NULL};
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;

    init_state = (struct m37_init_state){0};
    active = false;
    serial_write_string("M37 native desktop session startup acceptance:\n");
    if (!find_init_module(modules) || !x86_64_enable_nx() ||
        !x86_64_nx_enabled() || !process_init() || !task_init() ||
        !syscall_init() || !boring_ipc_system_init() ||
        !input_hardware_init() || !mount_root(&root)) {
        fail("subsystem/root init");
    }
    boring_ipc_syscall_use_bootstrap_stack();
    serial_write_string("m37-desktop: real BoringFS root mounted\n");

    if (!process_create(&init_state.process) || (init_state.process == NULL) ||
        (init_state.process->pid != 1ULL) ||
        !process_set_name(init_state.process, "boring-init") ||
        !process_set_cwd_text(init_state.process, &root, "/") ||
        !boring_elf_load(init_state.process, init_state.module,
                         init_state.module_size, (uintptr_t)M37_STACK_BASE,
                         M37_STACK_SIZE, &init_state.image) ||
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
        "m37-desktop: PID 1 scheduler task ready; desktop children must come from BoringFS\n");

    task_yield();
    fail("PID 1 unexpectedly returned to bootstrap");
}
