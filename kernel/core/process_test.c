#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/address_space.h>
#include <boring/cpu.h>
#include <boring/heap.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/process_test.h>
#include <boring/serial.h>
#include <boring/task.h>
#include <boring/timer.h>
#include <boring/vmm.h>

#define PROCESS_TEST_PATTERN_A 0xaaaaaaaaaaaaaaaaULL
#define PROCESS_TEST_PATTERN_B 0xbbbbbbbbbbbbbbbbULL
#define PROCESS_TEST_MIN_SLICES 3ULL
#define PROCESS_TEST_MIN_PREEMPTIONS 6ULL
#define PROCESS_TEST_MIN_PROGRESS 4096ULL
#define PROCESS_TEST_SPIN_LIMIT 500000000ULL
#define ADDRESS_SPACE_ROOT_MASK 0x000ffffffffff000ULL

struct process_task_result {
    struct process *process;
    uint64_t pattern;
    volatile uint64_t progress;
    volatile uint64_t slices;
    uintptr_t local_address;
    volatile bool process_ok;
    volatile bool stack_ok;
    volatile bool isolation_ok;
    volatile bool completed;
};

static void process_test_fail(const char *check) __attribute__((noreturn));

static void process_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Process/address-space self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void process_isolation_task(void *arg) {
    struct process_task_result *result =
        (struct process_task_result *)arg;
    volatile uint64_t local_counter = 1ULL;
    volatile uint64_t *const test_pointer =
        (volatile uint64_t *)address_space_test_virtual_address();
    const uintptr_t local_address = (uintptr_t)&local_counter;

    if ((result == NULL) || (result->process == NULL)) {
        return;
    }

    result->process_ok = true;
    result->stack_ok = true;
    result->isolation_ok = true;
    result->completed = false;
    result->local_address = local_address;

    for (;;) {
        const uint64_t current_slice = task_current_preempt_slices();

        if ((process_current() != result->process) ||
            (task_current_process_id() != result->process->pid)) {
            result->process_ok = false;
        }

        if (!task_current_stack_contains((const void *)&local_counter) ||
            ((uintptr_t)&local_counter != local_address)) {
            result->stack_ok = false;
        }

        if (*test_pointer != result->pattern) {
            result->isolation_ok = false;
        }

        *test_pointer = result->pattern;
        if (*test_pointer != result->pattern) {
            result->isolation_ok = false;
        }

        ++local_counter;
        result->progress = local_counter;
        result->slices = current_slice;

        if ((current_slice >= PROCESS_TEST_MIN_SLICES) &&
            (local_counter >= PROCESS_TEST_MIN_PROGRESS)) {
            break;
        }

        x86_64_pause();
    }

    result->completed = true;
}

static bool process_test_manual_isolation(
    struct process *process_a,
    struct process *process_b,
    uint64_t pattern_a,
    uint64_t pattern_b,
    bool *kernel_mappings_ok) {
    struct heap_stats heap_probe;
    volatile uint64_t *const test_pointer =
        (volatile uint64_t *)address_space_test_virtual_address();

    if ((process_a == NULL) || (process_b == NULL) ||
        (kernel_mappings_ok == NULL)) {
        return false;
    }

    *kernel_mappings_ok = false;

    if (!process_activate(process_a) ||
        !address_space_is_active(&process_a->address_space) ||
        !heap_get_stats(&heap_probe)) {
        return false;
    }
    serial_write_string("Process A kernel mappings active.\n");
    *test_pointer = pattern_a;
    if (*test_pointer != pattern_a) {
        return false;
    }

    if (!process_activate(process_b) ||
        !address_space_is_active(&process_b->address_space) ||
        !heap_get_stats(&heap_probe)) {
        return false;
    }
    serial_write_string("Process B kernel mappings active.\n");
    *test_pointer = pattern_b;
    if (*test_pointer != pattern_b) {
        return false;
    }

    if (!process_activate(process_a) || (*test_pointer != pattern_a)) {
        return false;
    }

    if (!process_activate(process_b) || (*test_pointer != pattern_b)) {
        return false;
    }

    if (!process_activate(process_bootstrap())) {
        return false;
    }

    *kernel_mappings_ok = true;
    return true;
}

void run_process_address_space_test(void) {
    struct process *const bootstrap = process_bootstrap();
    struct process *process_a = NULL;
    struct process *process_b = NULL;
    struct process_task_result task_a = { 0 };
    struct process_task_result task_b = { 0 };
    struct process_stats process_stats_before;
    struct process_stats process_stats_created;
    struct process_stats process_stats_final;
    struct address_space_stats address_stats_before;
    struct address_space_stats address_stats_manual;
    struct address_space_stats address_stats_preempt_before;
    struct address_space_stats address_stats_preempt_after;
    struct address_space_stats address_stats_final;
    struct task_stats task_stats_before;
    struct task_stats task_stats_after;
    struct task_stats task_stats_cleanup;
    struct pmm_stats pmm_before;
    struct pmm_stats pmm_after;
    struct heap_stats heap_before;
    struct heap_stats heap_after;
    struct vmm_stats vmm_before;
    struct vmm_stats vmm_after;
    const uintptr_t test_virtual = address_space_test_virtual_address();
    volatile uint64_t *const test_pointer =
        (volatile uint64_t *)test_virtual;
    uint64_t physical_a = 0ULL;
    uint64_t physical_b = 0ULL;
    uint64_t translated_a = 0ULL;
    uint64_t translated_b = 0ULL;
    uint64_t root_a;
    uint64_t root_b;
    uint64_t task_id_a = 0ULL;
    uint64_t task_id_b = 0ULL;
    uint64_t freed_stacks = 0ULL;
    uint64_t spins = 0ULL;
    uint64_t retained_heap_frames;
    uint64_t retained_vmm_frames;
    uint64_t expected_free_frames;
    uint64_t address_space_switches;
    uint64_t preemptive_cr3_switches;
    bool kernel_mappings_ok = false;

    if ((bootstrap == NULL) || !process_is_alive(bootstrap) ||
        (bootstrap->pid != KERNEL_BOOTSTRAP_PID) ||
        !process_get_stats(&process_stats_before) ||
        !address_space_get_stats(&address_stats_before) ||
        !task_get_stats(&task_stats_before) ||
        !pmm_get_stats(&pmm_before) || !heap_get_stats(&heap_before) ||
        !vmm_get_stats(&vmm_before) ||
        (process_stats_before.created_processes != 0ULL) ||
        (process_stats_before.active_processes != 0ULL) ||
        (process_stats_before.current_pid != KERNEL_BOOTSTRAP_PID) ||
        (task_stats_before.active_tasks != 0ULL) ||
        (task_stats_before.current_process_pid != KERNEL_BOOTSTRAP_PID) ||
        (address_stats_before.current_root_physical !=
         bootstrap->address_space.root_physical)) {
        serial_write_string("Process subsystem: FAILED\n");
        x86_64_halt_forever();
    }

    if (!process_create(&process_a) || !process_create(&process_b) ||
        (process_a == NULL) || (process_b == NULL) ||
        !process_get_stats(&process_stats_created) ||
        !address_space_get_stats(&address_stats_manual)) {
        process_test_fail("process-create");
    }

    root_a = process_a->address_space.root_physical;
    root_b = process_b->address_space.root_physical;

    if (!pmm_alloc_frame(&physical_a) || !pmm_alloc_frame(&physical_b)) {
        process_test_fail("address-space-create");
    }

    if (!address_space_map_page(&process_a->address_space, test_virtual,
                                physical_a, VMM_FLAG_WRITABLE) ||
        !address_space_map_page(&process_b->address_space, test_virtual,
                                physical_b, VMM_FLAG_WRITABLE) ||
        !address_space_translate(&process_a->address_space, test_virtual,
                                 &translated_a) ||
        !address_space_translate(&process_b->address_space, test_virtual,
                                 &translated_b)) {
        process_test_fail("address-space-create");
    }

    if (!address_space_kernel_mappings_valid(&process_a->address_space) ||
        !address_space_kernel_mappings_valid(&process_b->address_space)) {
        process_test_fail("kernel-mappings");
    }

    if (!process_test_manual_isolation(process_a, process_b,
                                       PROCESS_TEST_PATTERN_A,
                                       PROCESS_TEST_PATTERN_B,
                                       &kernel_mappings_ok) ||
        !address_space_get_stats(&address_stats_manual)) {
        process_test_fail("cr3-switch");
    }

    serial_write_string("Process subsystem:\n");
    serial_write_string("Bootstrap PID: 0\n");
    serial_write_string("Processes created: 2\n");
    serial_write_string("Address spaces created: 2\n");
    serial_write_string("Process model: online\n\n");

    serial_write_string("Address-space test:\n");
    serial_write_string("Test virtual address: ");
    serial_write_hex_u64((uint64_t)test_virtual);
    serial_write_string("\nProcess A PID: ");
    serial_write_u64(process_a->pid);
    serial_write_string("\nProcess B PID: ");
    serial_write_u64(process_b->pid);
    serial_write_string("\nProcess A root: ");
    serial_write_hex_u64(root_a);
    serial_write_string("\nProcess B root: ");
    serial_write_hex_u64(root_b);
    serial_write_string("\nProcess A physical frame: ");
    serial_write_hex_u64(physical_a);
    serial_write_string("\nProcess B physical frame: ");
    serial_write_hex_u64(physical_b);
    serial_write_string("\n\nProcess/address-space self-test:\n");

    if ((process_stats_created.created_processes != 2ULL) ||
        (process_stats_created.active_processes != 2ULL) ||
        (process_a->pid == KERNEL_BOOTSTRAP_PID) ||
        (process_b->pid == KERNEL_BOOTSTRAP_PID)) {
        process_test_fail("process-create");
    }
    serial_write_string("  process-create: PASS\n");

    if ((process_a->pid == process_b->pid) ||
        (process_a->pid != 1ULL) || (process_b->pid != 2ULL)) {
        process_test_fail("unique-pid");
    }
    serial_write_string("  unique-pid: PASS\n");

    if ((!process_a->address_space.initialized) ||
        (!process_b->address_space.initialized) ||
        (process_a->address_space.owned_table_count != 4ULL) ||
        (process_b->address_space.owned_table_count != 4ULL) ||
        (address_stats_manual.created_address_spaces != 2ULL)) {
        process_test_fail("address-space-create");
    }
    serial_write_string("  address-space-create: PASS\n");

    if ((root_a == root_b) ||
        (root_a == bootstrap->address_space.root_physical) ||
        (root_b == bootstrap->address_space.root_physical)) {
        process_test_fail("distinct-root");
    }
    serial_write_string("  distinct-root: PASS\n");

    if ((physical_a == physical_b) || (translated_a != physical_a) ||
        (translated_b != physical_b) || (translated_a == translated_b)) {
        process_test_fail("same-va-different-pa");
    }
    serial_write_string("  same-va-different-pa: PASS\n");

    if ((address_stats_manual.address_space_switches <
         (address_stats_before.address_space_switches + 5ULL)) ||
        (address_stats_manual.current_root_physical !=
         bootstrap->address_space.root_physical) ||
        ((x86_64_read_cr3() & ADDRESS_SPACE_ROOT_MASK) !=
         bootstrap->address_space.root_physical)) {
        process_test_fail("cr3-switch");
    }
    serial_write_string("  cr3-switch: PASS\n");

    if (!kernel_mappings_ok ||
        !address_space_kernel_mappings_valid(&process_a->address_space) ||
        !address_space_kernel_mappings_valid(&process_b->address_space)) {
        process_test_fail("kernel-mappings");
    }
    serial_write_string("  kernel-mappings: PASS\n");

    if (!process_activate(process_a) || (*test_pointer != PROCESS_TEST_PATTERN_A) ||
        !process_activate(process_b) || (*test_pointer != PROCESS_TEST_PATTERN_B) ||
        !process_activate(bootstrap)) {
        process_test_fail("process-a-isolation");
    }
    serial_write_string("  process-a-isolation: PASS\n");
    serial_write_string("  process-b-isolation: PASS\n");

    task_a.process = process_a;
    task_a.pattern = PROCESS_TEST_PATTERN_A;
    task_b.process = process_b;
    task_b.pattern = PROCESS_TEST_PATTERN_B;

    if (!task_create_preemptive_for_process(
            process_a, process_isolation_task, &task_a, &task_id_a) ||
        !task_create_preemptive_for_process(
            process_b, process_isolation_task, &task_b, &task_id_b) ||
        (task_id_a == task_id_b) ||
        !address_space_get_stats(&address_stats_preempt_before) ||
        !task_preemption_start()) {
        process_test_fail("preemptive-address-space-switch");
    }

    while ((!task_a.completed || !task_b.completed) &&
           (spins < PROCESS_TEST_SPIN_LIMIT)) {
        x86_64_pause();
        ++spins;
    }

    if ((!task_a.completed) || (!task_b.completed)) {
        process_test_fail("preemptive-address-space-switch");
    }

    if (!task_preemption_stop() || !task_get_stats(&task_stats_after) ||
        !address_space_get_stats(&address_stats_preempt_after)) {
        process_test_fail("bootstrap-return");
    }

    preemptive_cr3_switches =
        address_stats_preempt_after.address_space_switches -
        address_stats_preempt_before.address_space_switches;

    if ((!task_a.process_ok) || (!task_b.process_ok) ||
        (!task_a.stack_ok) || (!task_b.stack_ok) ||
        (!task_a.isolation_ok) || (!task_b.isolation_ok) ||
        (task_a.progress < PROCESS_TEST_MIN_PROGRESS) ||
        (task_b.progress < PROCESS_TEST_MIN_PROGRESS) ||
        (task_a.slices < PROCESS_TEST_MIN_SLICES) ||
        (task_b.slices < PROCESS_TEST_MIN_SLICES) ||
        (task_stats_after.preemptions < PROCESS_TEST_MIN_PREEMPTIONS) ||
        (task_stats_after.scheduler_ticks < PROCESS_TEST_MIN_PREEMPTIONS) ||
        (task_stats_after.cooperative_yield_calls !=
         task_stats_before.cooperative_yield_calls) ||
        (preemptive_cr3_switches != task_stats_after.preemptions)) {
        process_test_fail("preemptive-address-space-switch");
    }
    serial_write_string("  preemptive-address-space-switch: PASS\n");

    if ((process_current() != bootstrap) ||
        (task_stats_after.current_task_id != KERNEL_BOOTSTRAP_TASK_ID) ||
        (task_stats_after.current_process_pid != KERNEL_BOOTSTRAP_PID) ||
        task_stats_after.preemption_enabled ||
        (task_stats_after.finished_resume_count != 0ULL) ||
        (task_stats_after.scheduler_fault_count != 0ULL) ||
        !address_space_is_active(&bootstrap->address_space)) {
        process_test_fail("bootstrap-return");
    }
    serial_write_string("  bootstrap-return: PASS\n");

    if (!process_activate(process_a) || (*test_pointer != PROCESS_TEST_PATTERN_A)) {
        process_test_fail("process-a-isolation");
    }
    if (!process_activate(process_b) || (*test_pointer != PROCESS_TEST_PATTERN_B)) {
        process_test_fail("process-b-isolation");
    }
    if (!process_activate(bootstrap)) {
        process_test_fail("bootstrap-return");
    }

    if (!task_finished_stacks_valid() ||
        !task_cleanup_finished(&freed_stacks) || (freed_stacks != 2ULL) ||
        !task_get_stats(&task_stats_cleanup) ||
        (task_stats_cleanup.active_tasks != 0ULL) ||
        !process_mark_finished(process_a) ||
        !process_mark_finished(process_b) ||
        !address_space_unmap_page(&process_a->address_space, test_virtual) ||
        !address_space_unmap_page(&process_b->address_space, test_virtual) ||
        !pmm_free_frame(physical_a) || !pmm_free_frame(physical_b) ||
        !process_destroy(process_a) || !process_destroy(process_b) ||
        !process_get_stats(&process_stats_final) ||
        !address_space_get_stats(&address_stats_final)) {
        process_test_fail("address-space-cleanup");
    }

    if ((process_stats_final.active_processes != 0ULL) ||
        (process_stats_final.current_pid != KERNEL_BOOTSTRAP_PID) ||
        (process_stats_final.finished_processes != 2ULL) ||
        (address_stats_final.destroyed_address_spaces !=
         (address_stats_before.destroyed_address_spaces + 2ULL)) ||
        (address_stats_final.current_root_physical !=
         bootstrap->address_space.root_physical) ||
        !address_space_is_active(&bootstrap->address_space)) {
        process_test_fail("address-space-cleanup");
    }
    serial_write_string("  address-space-cleanup: PASS\n");

    if (!pmm_get_stats(&pmm_after) || !heap_get_stats(&heap_after) ||
        !vmm_get_stats(&vmm_after) ||
        (heap_after.mapped_pages < heap_before.mapped_pages) ||
        (vmm_after.owned_page_table_frames <
         vmm_before.owned_page_table_frames)) {
        process_test_fail("pmm-bookkeeping");
    }

    retained_heap_frames = heap_after.mapped_pages - heap_before.mapped_pages;
    retained_vmm_frames = vmm_after.owned_page_table_frames -
                          vmm_before.owned_page_table_frames;
    if (pmm_before.free_frames <
        (retained_heap_frames + retained_vmm_frames)) {
        process_test_fail("pmm-bookkeeping");
    }
    expected_free_frames = pmm_before.free_frames - retained_heap_frames -
                           retained_vmm_frames;

    if ((pmm_after.free_frames != expected_free_frames) ||
        (pmm_after.usable_frames != pmm_before.usable_frames) ||
        (heap_after.allocation_count != heap_before.allocation_count) ||
        (heap_after.used_bytes != heap_before.used_bytes)) {
        process_test_fail("pmm-bookkeeping");
    }
    serial_write_string("  pmm-bookkeeping: PASS\n");

    address_space_switches = address_stats_final.address_space_switches -
                             address_stats_before.address_space_switches;

    serial_write_string("Address-space switches: ");
    serial_write_u64(address_space_switches);
    serial_write_string("\nPreemptive CR3 switches: ");
    serial_write_u64(preemptive_cr3_switches);
    serial_write_string("\nProcess A slices: ");
    serial_write_u64(task_a.slices);
    serial_write_string("\nProcess B slices: ");
    serial_write_u64(task_b.slices);
    serial_write_string("\nTask stacks freed: ");
    serial_write_u64(freed_stacks);
    serial_write_string("\n\nBoringKernel process/address-space test passed.\n");
}
