#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/display_test.h>
#include <boring/wm_test.h>
#include <boring/elf_loader.h>
#include <boring/framebuffer_user.h>
#include <boring/i8042.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/ipc_syscall.h>
#include <boring/irq.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/task.h>
#include <boring/user_memory.h>
#include <boring/vmm.h>

#define DISPLAY_TEST_PROCESS_COUNT 5U
#define DISPLAY_TEST_STACK_BASE 0x0000000040020000ULL
#define DISPLAY_TEST_STACK_SIZE ((size_t)BORING_ELF_PAGE_SIZE)

struct display_module_spec {
    const char *path;
    const char *string;
    const char *process_name;
};

struct display_test_process_state {
    struct process *process;
    struct boring_elf_image image;
    const uint8_t *module;
    size_t module_size;
    uint64_t task_id;
    bool image_loaded;
};

static const struct display_module_spec display_specs[DISPLAY_TEST_PROCESS_COUNT] = {
    { "/boot/user/boring-display.elf", "boringos-boring-display", "boring-display" },
    { "/boot/user/boringwm.elf", "boringos-boringwm", "boringwm" },
    { "/boot/user/wm-client-a.elf", "boringos-wm-client-a", "wm-client-a" },
    { "/boot/user/wm-client-b.elf", "boringos-wm-client-b", "wm-client-b" },
    { "/boot/user/wm-client-c.elf", "boringos-wm-client-c", "wm-client-c" }
};

static struct display_test_process_state
    display_processes[DISPLAY_TEST_PROCESS_COUNT];
static bool display_test_active;

void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static void display_test_fail(const char *check) __attribute__((noreturn));
static void display_test_fail(const char *check) {
    serial_write_string("M35 display acceptance FAILED: ");
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

static bool text_equals(const char *actual, const char *expected) {
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

static bool module_bounds(const struct boring_limine_file *module) {
    struct vmm_stats stats;
    uintptr_t address;
    uintptr_t last;

    if ((module == NULL) || !hhdm_pointer(module) || !vmm_get_stats(&stats)) {
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
    return canonical_higher(last) && ((uint64_t)address >= stats.hhdm_offset);
}

static bool modules_find(const struct boring_limine_module_response *response) {
    bool found[DISPLAY_TEST_PROCESS_COUNT] = { false };
    size_t module_index;

    if ((response == NULL) ||
        (response->module_count != (uint64_t)DISPLAY_TEST_PROCESS_COUNT) ||
        !hhdm_pointer(response->modules)) {
        return false;
    }
    for (module_index = 0U; module_index < DISPLAY_TEST_PROCESS_COUNT;
         ++module_index) {
        const struct boring_limine_file *module = response->modules[module_index];
        size_t spec_index;
        bool matched = false;

        if (!module_bounds(module)) {
            return false;
        }
        for (spec_index = 0U; spec_index < DISPLAY_TEST_PROCESS_COUNT;
             ++spec_index) {
            if (text_equals(module->path, display_specs[spec_index].path) &&
                text_equals(module->string, display_specs[spec_index].string)) {
                if (found[spec_index]) {
                    return false;
                }
                display_processes[spec_index].module =
                    (const uint8_t *)module->address;
                display_processes[spec_index].module_size = (size_t)module->size;
                found[spec_index] = true;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return found[0] && found[1] && found[2] && found[3] && found[4];
}

static struct display_test_process_state *state_for_process(struct process *process) {
    size_t index;

    for (index = 0U; index < DISPLAY_TEST_PROCESS_COUNT; ++index) {
        if (display_processes[index].process == process) {
            return &display_processes[index];
        }
    }
    return NULL;
}

bool boring_display_test_process_exit_prepare(struct process *process) {
    struct display_test_process_state *state;

    if (!display_test_active || (process == NULL)) {
        return false;
    }
    state = state_for_process(process);
    if ((state == NULL) || !state->image_loaded) {
        return false;
    }
    if (!boring_elf_unload(&state->image)) {
        display_test_fail("ELF unload during process exit");
    }
    state->image_loaded = false;
    return true;
}

static void user_entry(void *argument) __attribute__((noreturn));
static void user_entry(void *argument) {
    struct display_test_process_state *state =
        (struct display_test_process_state *)argument;
    struct descriptor_stats descriptors;

    if ((state == NULL) || (state->process == NULL) || !state->image_loaded ||
        (process_current() != state->process) || !descriptor_get_stats(&descriptors)) {
        display_test_fail("task user-entry state");
    }
    serial_write_string("wm-test: enter CPL3 pid ");
    serial_write_u64(state->process->pid);
    serial_write_string(" cr3 ");
    serial_write_hex_u64(state->process->address_space.root_physical);
    serial_write_string("\n");
    x86_64_enter_ring3(state->image.entry,
                       state->image.stack_top,
                       descriptors.user_code_selector,
                       descriptors.user_data_selector,
                       0U);
}

static bool input_hardware_init(void) {
    struct i8042_state state = { false };

    x86_64_interrupts_disable();
    if (!boring_input_init() || !irq_init()) {
        return false;
    }
    (void)i8042_init(&state);
    if (!state.keyboard_online || !state.mouse_online ||
        !irq_unmask_input(state.keyboard_online, state.mouse_online)) {
        return false;
    }
    serial_write_string("wm-test: real PS/2 keyboard and mouse path online\n");
    return true;
}

void wm_test_run(const struct boring_limine_module_response *modules) {
    struct pmm_stats before;
    struct pmm_stats during;
    struct pmm_stats after;
    struct task_stats task_state;
    struct boring_ipc_stats ipc_stats;
    struct user_memory_global_stats memory_stats;
    struct boring_input_stats input_stats;
    struct boring_framebuffer_user_stats framebuffer_stats;
    uint64_t freed_stacks = 0ULL;
    size_t index;

    display_test_active = false;
    for (index = 0U; index < DISPLAY_TEST_PROCESS_COUNT; ++index) {
        display_processes[index].process = NULL;
        display_processes[index].module = NULL;
        display_processes[index].module_size = 0U;
        display_processes[index].task_id = 0ULL;
        display_processes[index].image_loaded = false;
    }
    serial_write_string("M35 native BoringWM acceptance:\n");
    if (!modules_find(modules)) {
        display_test_fail("boot modules");
    }
    if (!x86_64_enable_nx() || !x86_64_nx_enabled() ||
        !process_init() || !task_init() || !syscall_init() ||
        !boring_ipc_system_init() || !input_hardware_init() ||
        !pmm_get_stats(&before)) {
        display_test_fail("subsystem init");
    }
    boring_ipc_syscall_use_bootstrap_stack();

    for (index = 0U; index < DISPLAY_TEST_PROCESS_COUNT; ++index) {
        struct process *process = NULL;

        if (!process_create(&process) || (process == NULL) ||
            (process->pid != (uint64_t)(index + 1U)) ||
            !process_set_name(process, display_specs[index].process_name) ||
            !boring_elf_load(process,
                             display_processes[index].module,
                             display_processes[index].module_size,
                             (uintptr_t)DISPLAY_TEST_STACK_BASE,
                             DISPLAY_TEST_STACK_SIZE,
                             &display_processes[index].image)) {
            display_test_fail("process/image creation");
        }
        display_processes[index].process = process;
        display_processes[index].image_loaded = true;
    }
    if (!pmm_get_stats(&during) || (during.free_frames >= before.free_frames)) {
        display_test_fail("PMM allocation witness");
    }
    for (index = 0U; index < DISPLAY_TEST_PROCESS_COUNT; ++index) {
        if (!task_create_for_process(display_processes[index].process,
                                     user_entry, &display_processes[index],
                                     &display_processes[index].task_id)) {
            display_test_fail("task creation");
        }
    }

    display_test_active = true;
    serial_write_string("wm-test: five distinct processes ready\n");
    task_yield();
    display_test_active = false;
    boring_ipc_syscall_use_bootstrap_stack();

    if (!task_get_stats(&task_state) ||
        (task_state.finished_tasks != (uint64_t)DISPLAY_TEST_PROCESS_COUNT) ||
        (task_state.active_tasks != (uint64_t)DISPLAY_TEST_PROCESS_COUNT) ||
        (task_state.current_task_id != KERNEL_BOOTSTRAP_TASK_ID) ||
        !boring_ipc_get_stats(&ipc_stats) ||
        (ipc_stats.live_services != 0U) ||
        (ipc_stats.live_connections != 0U) ||
        (ipc_stats.queued_messages != 0U) ||
        (ipc_stats.retained_buffer_attachments != 0U) ||
        !user_memory_get_global_stats(&memory_stats) ||
        (memory_stats.active_objects != 0U) ||
        !boring_input_get_stats(&input_stats) || input_stats.owned ||
        !boring_framebuffer_user_get_stats(&framebuffer_stats) ||
        framebuffer_stats.claimed || (framebuffer_stats.presents < 5ULL)) {
        display_test_fail("post-exit resource accounting");
    }
    serial_write_string("wm-test: IPC/input/framebuffer/M32 resources reclaimed\n");

    if (!task_cleanup_finished(&freed_stacks) ||
        (freed_stacks != (uint64_t)DISPLAY_TEST_PROCESS_COUNT)) {
        display_test_fail("task stack cleanup");
    }
    for (index = 0U; index < DISPLAY_TEST_PROCESS_COUNT; ++index) {
        if ((display_processes[index].process == NULL) ||
            (display_processes[index].process->state != PROCESS_FINISHED) ||
            !process_destroy(display_processes[index].process)) {
            display_test_fail("process destroy");
        }
        display_processes[index].process = NULL;
    }
    if (!pmm_get_stats(&after) || (after.free_frames <= during.free_frames)) {
        display_test_fail("PMM recovery");
    }
    serial_write_string("wm-test: pmm before=");
    serial_write_u64(before.free_frames);
    serial_write_string(" during=");
    serial_write_u64(during.free_frames);
    serial_write_string(" after=");
    serial_write_u64(after.free_frames);
    serial_write_string("\n");
    serial_write_string("wm-test: process-exit cleanup passed\n");
    serial_write_string("M35 native BoringWM acceptance passed.\n");
    x86_64_halt_forever();
}
