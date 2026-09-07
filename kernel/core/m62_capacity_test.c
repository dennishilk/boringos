#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <boring/address_space.h>
#include <boring/cpu.h>
#include <boring/heap.h>
#include <boring/m62_capacity_test.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/task.h>

#define M62_STRESS_CONCURRENCY 24U
#define M62_STRESS_CYCLES 3U

struct m62_task_probe { bool ran; };
static void m62_fail(const char *reason) __attribute__((noreturn));

static void m62_fail(const char *reason) {
    serial_write_string("M62 CAPACITY TEST FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void m62_task_entry(void *argument) {
    struct m62_task_probe *const probe = (struct m62_task_probe *)argument;
    if (probe == NULL) { m62_fail("null task probe"); }
    probe->ran = true;
    task_exit_current_process();
}

void m62_capacity_test_run(void) {
    struct process *processes[M62_STRESS_CONCURRENCY];
    struct m62_task_probe probes[M62_STRESS_CONCURRENCY];
    struct process_stats process_baseline;
    struct task_stats task_baseline;
    struct heap_stats heap_baseline;
    struct address_space_stats address_baseline;
    uint32_t cycle;

    if (!process_init() || !task_init() ||
        !process_get_stats(&process_baseline) ||
        !task_get_stats(&task_baseline) ||
        !heap_get_stats(&heap_baseline) ||
        !address_space_get_stats(&address_baseline) ||
        (process_baseline.active_processes != 0ULL) ||
        (task_baseline.active_tasks != 0ULL)) {
        m62_fail("baseline initialization");
    }

    for (cycle = 0U; cycle < M62_STRESS_CYCLES; ++cycle) {
        struct process_stats process_live, process_after;
        struct task_stats task_live, task_finished, task_after;
        struct heap_stats heap_after;
        struct address_space_stats address_after;
        uint64_t task_id;
        size_t index;

        for (index = 0U; index < (size_t)M62_STRESS_CONCURRENCY; ++index) {
            processes[index] = NULL;
            probes[index].ran = false;
            if (!process_create(&processes[index]) ||
                (processes[index] == NULL) ||
                !task_create_for_process(processes[index], m62_task_entry,
                                         &probes[index], &task_id)) {
                m62_fail("create concurrency");
            }
        }
        if (!process_get_stats(&process_live) || !task_get_stats(&task_live) ||
            (process_live.active_processes != (uint64_t)M62_STRESS_CONCURRENCY) ||
            (task_live.active_tasks != (uint64_t)M62_STRESS_CONCURRENCY)) {
            m62_fail("live concurrency accounting");
        }
        serial_write_string("M62 CAPACITY cycle=");
        serial_write_u64((uint64_t)cycle + 1ULL);
        serial_write_string(" live_processes=");
        serial_write_u64(process_live.active_processes);
        serial_write_string(" live_tasks=");
        serial_write_u64(task_live.active_tasks);
        serial_write_string("\n");

        task_yield();

        if (!task_get_stats(&task_finished) ||
            (task_finished.active_tasks != (uint64_t)M62_STRESS_CONCURRENCY) ||
            !task_finished_stacks_valid()) {
            m62_fail("finished task visibility");
        }
        for (index = 0U; index < (size_t)M62_STRESS_CONCURRENCY; ++index) {
            if (!probes[index].ran || (processes[index] == NULL) ||
                (processes[index]->state != PROCESS_FINISHED)) {
                m62_fail("scheduler coverage");
            }
        }
        serial_write_string("M62 CAPACITY cycle=");
        serial_write_u64((uint64_t)cycle + 1ULL);
        serial_write_string(" schedulable=24\n");

        for (index = 0U; index < (size_t)M62_STRESS_CONCURRENCY; ++index) {
            if (!task_reap_finished_process(processes[index]) ||
                !process_destroy(processes[index])) {
                m62_fail("dynamic reap");
            }
            processes[index] = NULL;
        }

        if (!process_get_stats(&process_after) || !task_get_stats(&task_after) ||
            !heap_get_stats(&heap_after) || !address_space_get_stats(&address_after) ||
            (process_after.active_processes != process_baseline.active_processes) ||
            (task_after.active_tasks != task_baseline.active_tasks) ||
            (heap_after.allocation_count != heap_baseline.allocation_count) ||
            (address_after.created_address_spaces !=
             address_baseline.created_address_spaces +
             ((uint64_t)cycle + 1ULL) * (uint64_t)M62_STRESS_CONCURRENCY) ||
            (address_after.destroyed_address_spaces !=
             address_baseline.destroyed_address_spaces +
             ((uint64_t)cycle + 1ULL) * (uint64_t)M62_STRESS_CONCURRENCY)) {
            m62_fail("post-churn baseline");
        }
        serial_write_string("M62 CAPACITY cycle=");
        serial_write_u64((uint64_t)cycle + 1ULL);
        serial_write_string(" baseline_processes=");
        serial_write_u64(process_after.active_processes);
        serial_write_string(" baseline_tasks=");
        serial_write_u64(task_after.active_tasks);
        serial_write_string(" heap_allocations=");
        serial_write_u64(heap_after.allocation_count);
        serial_write_string("\n");
    }

    serial_write_string("M62 CAPACITY SUCCESS max_processes=24 max_tasks=24 cycles=3 baseline_restored=yes\n");
    x86_64_halt_forever();
}
