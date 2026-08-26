#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_loader.h>
#include <boring/ipc.h>
#include <boring/ipc_syscall.h>
#include <boring/ipc_test.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/task.h>
#include <boring/user_memory.h>
#include <boring/vmm.h>

#define IPC_TEST_PROCESS_COUNT 3U
#define IPC_TEST_MODULE_PATH "/boot/user/ipc-test.elf"
#define IPC_TEST_MODULE_STRING "boringos-ipc-test"
#define IPC_TEST_STACK_BASE 0x0000000040010000ULL
#define IPC_TEST_STACK_SIZE ((size_t)BORING_ELF_PAGE_SIZE)

struct ipc_test_process_state {
    struct process *process;
    struct boring_elf_image image;
    uint64_t task_id;
    bool image_loaded;
};

static struct ipc_test_process_state ipc_test_processes[IPC_TEST_PROCESS_COUNT];
static bool ipc_test_active;

void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static void ipc_test_fail(const char *check) __attribute__((noreturn));
static void ipc_test_fail(const char *check) {
    serial_write_string("M33 IPC acceptance FAILED: ");
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

static bool module_find(const struct boring_limine_module_response *response,
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
    if (!canonical_higher(last) || ((uint64_t)address < stats.hhdm_offset) ||
        !text_equals(module->path, IPC_TEST_MODULE_PATH) ||
        !text_equals(module->string, IPC_TEST_MODULE_STRING)) {
        return false;
    }
    *bytes = (const uint8_t *)module->address;
    *size = (size_t)module->size;
    return true;
}

static struct ipc_test_process_state *state_for_process(struct process *process) {
    size_t index;

    for (index = 0U; index < (size_t)IPC_TEST_PROCESS_COUNT; ++index) {
        if (ipc_test_processes[index].process == process) {
            return &ipc_test_processes[index];
        }
    }
    return NULL;
}

bool boring_ipc_test_process_exit_prepare(struct process *process) {
    struct ipc_test_process_state *state;

    if (!ipc_test_active || (process == NULL)) {
        return false;
    }
    state = state_for_process(process);
    if ((state == NULL) || !state->image_loaded) {
        return false;
    }
    if (!boring_elf_unload(&state->image)) {
        ipc_test_fail("ELF unload during process exit");
    }
    state->image_loaded = false;
    return true;
}

static void ipc_test_user_entry(void *argument) __attribute__((noreturn));
static void ipc_test_user_entry(void *argument) {
    struct ipc_test_process_state *state =
        (struct ipc_test_process_state *)argument;
    struct descriptor_stats descriptors;

    if ((state == NULL) || (state->process == NULL) ||
        !state->image_loaded || (process_current() != state->process) ||
        !descriptor_get_stats(&descriptors)) {
        ipc_test_fail("task user-entry state");
    }
    serial_write_string("ipc-test: enter CPL3 pid ");
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

static bool process_name_for_index(size_t index, const char **name_out) {
    static const char *const names[IPC_TEST_PROCESS_COUNT] = {
        "ipc-server", "ipc-client-a", "ipc-client-b"
    };

    if ((index >= (size_t)IPC_TEST_PROCESS_COUNT) || (name_out == NULL)) {
        return false;
    }
    *name_out = names[index];
    return true;
}

void ipc_test_run(const struct boring_limine_module_response *modules) {
    const uint8_t *module = NULL;
    size_t module_size = 0U;
    struct pmm_stats before;
    struct pmm_stats during;
    struct pmm_stats after;
    struct task_stats task_state;
    struct boring_ipc_stats ipc_stats;
    struct user_memory_global_stats memory_stats;
    uint64_t freed_stacks = 0ULL;
    size_t index;

    ipc_test_active = false;
    for (index = 0U; index < (size_t)IPC_TEST_PROCESS_COUNT; ++index) {
        ipc_test_processes[index].process = NULL;
        ipc_test_processes[index].task_id = 0ULL;
        ipc_test_processes[index].image_loaded = false;
    }

    serial_write_string("M33 native IPC/service acceptance:\n");
    if (!module_find(modules, &module, &module_size)) {
        ipc_test_fail("boot module");
    }
    if (!x86_64_enable_nx() || !x86_64_nx_enabled()) {
        ipc_test_fail("NX");
    }
    if (!process_init() || !task_init() || !syscall_init() ||
        !boring_ipc_system_init() || !pmm_get_stats(&before)) {
        ipc_test_fail("subsystem init");
    }
    boring_ipc_syscall_use_bootstrap_stack();

    for (index = 0U; index < (size_t)IPC_TEST_PROCESS_COUNT; ++index) {
        struct process *process = NULL;
        const char *name = NULL;

        if (!process_name_for_index(index, &name) ||
            !process_create(&process) || (process == NULL) ||
            (process->pid != (uint64_t)(index + 1U)) ||
            !process_set_name(process, name) ||
            !boring_elf_load(process, module, module_size,
                             (uintptr_t)IPC_TEST_STACK_BASE,
                             IPC_TEST_STACK_SIZE,
                             &ipc_test_processes[index].image)) {
            ipc_test_fail("process/image creation");
        }
        ipc_test_processes[index].process = process;
        ipc_test_processes[index].image_loaded = true;
    }

    if (!pmm_get_stats(&during) || (during.free_frames >= before.free_frames)) {
        ipc_test_fail("PMM allocation witness");
    }

    for (index = 0U; index < (size_t)IPC_TEST_PROCESS_COUNT; ++index) {
        if (!task_create_for_process(
                ipc_test_processes[index].process,
                ipc_test_user_entry,
                &ipc_test_processes[index],
                &ipc_test_processes[index].task_id)) {
            ipc_test_fail("task creation");
        }
    }

    ipc_test_active = true;
    serial_write_string("ipc-test: three distinct processes ready\n");
    task_yield();
    ipc_test_active = false;
    boring_ipc_syscall_use_bootstrap_stack();

    if (!task_get_stats(&task_state) ||
        (task_state.finished_tasks != (uint64_t)IPC_TEST_PROCESS_COUNT) ||
        (task_state.active_tasks != (uint64_t)IPC_TEST_PROCESS_COUNT) ||
        (task_state.current_task_id != KERNEL_BOOTSTRAP_TASK_ID) ||
        !boring_ipc_get_stats(&ipc_stats) ||
        (ipc_stats.live_services != 0U) ||
        (ipc_stats.live_connections != 0U) ||
        (ipc_stats.queued_messages != 0U) ||
        (ipc_stats.retained_buffer_attachments != 0U) ||
        !user_memory_get_global_stats(&memory_stats) ||
        (memory_stats.active_objects != 0U)) {
        ipc_test_fail("post-exit resource accounting");
    }
    serial_write_string("ipc-test: IPC and M32 resources reclaimed\n");

    if (!task_cleanup_finished(&freed_stacks) ||
        (freed_stacks != (uint64_t)IPC_TEST_PROCESS_COUNT)) {
        ipc_test_fail("task stack cleanup");
    }
    for (index = 0U; index < (size_t)IPC_TEST_PROCESS_COUNT; ++index) {
        if ((ipc_test_processes[index].process == NULL) ||
            (ipc_test_processes[index].process->state != PROCESS_FINISHED) ||
            !process_destroy(ipc_test_processes[index].process)) {
            ipc_test_fail("process destroy");
        }
        ipc_test_processes[index].process = NULL;
    }
    if (!pmm_get_stats(&after) || (after.free_frames <= during.free_frames)) {
        ipc_test_fail("PMM recovery");
    }

    serial_write_string("ipc-test: pmm before=");
    serial_write_u64(before.free_frames);
    serial_write_string(" during=");
    serial_write_u64(during.free_frames);
    serial_write_string(" after=");
    serial_write_u64(after.free_frames);
    serial_write_string("\n");
    serial_write_string("ipc-test: process-local handle isolation passed\n");
    serial_write_string("ipc-test: process-exit cleanup passed\n");
    serial_write_string("M33 native IPC/service acceptance passed.\n");
    x86_64_halt_forever();
}
