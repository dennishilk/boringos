#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/context.h>
#include <boring/cpu.h>
#include <boring/heap.h>
#include <boring/irq.h>
#include <boring/preemption_test.h>
#include <boring/serial.h>
#include <boring/task.h>
#include <boring/timer.h>

#define PREEMPT_TEST_MIN_SLICES 3ULL
#define PREEMPT_TEST_MIN_PREEMPTIONS 6ULL
#define PREEMPT_TEST_MIN_PROGRESS 4096ULL
#define PREEMPT_TEST_BOOTSTRAP_SPIN_LIMIT 500000000ULL
#define PREEMPT_TEST_PROBE_SPIN_LIMIT 500000000ULL

struct preemptive_task_result {
    uint64_t id;
    uint64_t progress;
    uint64_t slices;
    uint64_t resumes;
    uint64_t checksum;
    uintptr_t local_counter_address;
    uintptr_t checksum_address;
    bool stack_ok;
    bool local_state_ok;
    bool register_ok;
    bool completed;
    bool register_probe_task;
};

static void preemptive_task_test_fail(const char *check)
    __attribute__((noreturn));

static void preemptive_task_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Preemptive scheduling self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void preemptive_record_resume(struct preemptive_task_result *result,
                                     uint64_t *last_slice,
                                     uint64_t current_slice) {
    if (current_slice < *last_slice) {
        result->local_state_ok = false;
        return;
    }

    if (current_slice > *last_slice) {
        result->resumes += current_slice - *last_slice;
        *last_slice = current_slice;
    }
}

static void preemptive_task_entry(void *arg) {
    struct preemptive_task_result *result =
        (struct preemptive_task_result *)arg;
    volatile uint64_t local_counter = 1ULL;
    volatile uint64_t checksum = 0x424f52494e470000ULL;
    const uintptr_t local_counter_address = (uintptr_t)&local_counter;
    const uintptr_t checksum_address = (uintptr_t)&checksum;
    uint64_t last_slice;

    if (result == NULL) {
        return;
    }

    result->stack_ok = true;
    result->local_state_ok = true;
    result->register_ok = true;
    result->completed = false;
    result->local_counter_address = local_counter_address;
    result->checksum_address = checksum_address;

    if ((task_current_id() != result->id) ||
        !task_current_stack_contains((const void *)&local_counter) ||
        !task_current_stack_contains((const void *)&checksum) ||
        (local_counter_address == checksum_address)) {
        result->stack_ok = false;
    }

    last_slice = task_current_preempt_slices();
    result->slices = last_slice;
    result->progress = local_counter;
    result->checksum = checksum;

    if (result->register_probe_task) {
        if (!x86_64_context_test_preemptive_gprs()) {
            result->register_ok = false;
        }
        preemptive_record_resume(result, &last_slice,
                                 task_current_preempt_slices());
    } else {
        uint64_t spins = 0ULL;

        while ((!x86_64_preemption_probe_is_armed()) &&
               (spins < PREEMPT_TEST_PROBE_SPIN_LIMIT)) {
            const uint64_t current_slice = task_current_preempt_slices();

            preemptive_record_resume(result, &last_slice, current_slice);
            x86_64_pause();
            ++spins;
        }

        if (!x86_64_preemption_probe_is_armed()) {
            result->register_ok = false;
        } else {
            x86_64_preemption_probe_release();
        }
    }

    for (;;) {
        const uint64_t current_slice = task_current_preempt_slices();

        preemptive_record_resume(result, &last_slice, current_slice);

        if ((task_current_id() != result->id) ||
            !task_current_stack_contains((const void *)&local_counter) ||
            !task_current_stack_contains((const void *)&checksum) ||
            ((uintptr_t)&local_counter != local_counter_address) ||
            ((uintptr_t)&checksum != checksum_address)) {
            result->stack_ok = false;
        }

        if ((local_counter == 0ULL) || (checksum == 0ULL)) {
            result->local_state_ok = false;
        }

        ++local_counter;
        checksum = (checksum << 7) ^ (checksum >> 3) ^
                   local_counter ^ result->id;

        result->progress = local_counter;
        result->checksum = checksum;
        result->slices = current_slice;

        if ((current_slice >= PREEMPT_TEST_MIN_SLICES) &&
            (local_counter >= PREEMPT_TEST_MIN_PROGRESS)) {
            break;
        }

        x86_64_pause();
    }

    result->completed = true;
}

void run_preemptive_task_test(void) {
    struct preemptive_task_result task_a = { 0 };
    struct preemptive_task_result task_b = { 0 };
    struct task_stats task_stats_before;
    struct task_stats task_stats_created;
    struct task_stats task_stats_after;
    struct task_stats task_stats_cleanup;
    struct heap_stats heap_before;
    struct heap_stats heap_after;
    struct irq_stats irq_before;
    struct irq_stats irq_after;
    uint64_t ticks_before;
    uint64_t ticks_after;
    uint64_t timer_ticks_during_test;
    uint64_t timer_irqs_during_test;
    uint64_t freed_stacks = 0ULL;
    uint64_t spins = 0ULL;
    bool interrupts_were_enabled;

    task_a.register_probe_task = true;
    task_b.register_probe_task = false;

    if (!heap_get_stats(&heap_before) ||
        !task_get_stats(&task_stats_before) ||
        (task_stats_before.active_tasks != 0ULL) ||
        (task_stats_before.current_task_id != KERNEL_BOOTSTRAP_TASK_ID) ||
        task_stats_before.preemption_enabled ||
        !irq_get_stats(&irq_before)) {
        serial_write_string("Preemptive scheduler: FAILED\n");
        x86_64_halt_forever();
    }

    x86_64_preemption_probe_reset();

    if (!task_create_preemptive(preemptive_task_entry,
                                &task_a, &task_a.id) ||
        !task_create_preemptive(preemptive_task_entry,
                                &task_b, &task_b.id) ||
        !task_get_stats(&task_stats_created) ||
        (task_stats_created.active_tasks != 2ULL) ||
        (task_stats_created.current_task_id != KERNEL_BOOTSTRAP_TASK_ID) ||
        (task_stats_created.created_tasks !=
         (task_stats_before.created_tasks + 2ULL)) ||
        (task_a.id == 0ULL) || (task_b.id == 0ULL) ||
        (task_a.id == task_b.id)) {
        serial_write_string("Preemptive scheduler: FAILED\n");
        x86_64_halt_forever();
    }

    ticks_before = timer_ticks();
    if (!task_preemption_start()) {
        serial_write_string("Preemptive scheduler: FAILED\n");
        x86_64_halt_forever();
    }

    while ((!task_a.completed || !task_b.completed) &&
           (spins < PREEMPT_TEST_BOOTSTRAP_SPIN_LIMIT)) {
        x86_64_pause();
        ++spins;
    }

    if ((!task_a.completed) || (!task_b.completed)) {
        preemptive_task_test_fail("task-progress-timeout");
    }

    if (!task_preemption_stop()) {
        preemptive_task_test_fail("bootstrap-return");
    }

    interrupts_were_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();
    ticks_after = timer_ticks();
    if (!irq_get_stats(&irq_after) || !task_get_stats(&task_stats_after)) {
        preemptive_task_test_fail("stats");
    }
    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }

    if ((ticks_after < ticks_before) ||
        (irq_after.timer_irq_count < irq_before.timer_irq_count)) {
        preemptive_task_test_fail("timer-delivery");
    }
    timer_ticks_during_test = ticks_after - ticks_before;
    timer_irqs_during_test =
        irq_after.timer_irq_count - irq_before.timer_irq_count;

    serial_write_string("Preemptive scheduler:\n");
    serial_write_string("Policy: round-robin\n");
    serial_write_string("Timer source: PIT IRQ0\n");
    serial_write_string("Timer vector: 32\n");
    serial_write_string("Quantum: 1 tick\n");
    serial_write_string("Preemption: enabled during test\n\n");
    serial_write_string("Preemption self-test:\n");

    if ((!task_a.completed) ||
        (task_a.progress < PREEMPT_TEST_MIN_PROGRESS)) {
        preemptive_task_test_fail("task-a-progress");
    }
    serial_write_string("  task-a-progress: PASS\n");

    if ((!task_b.completed) ||
        (task_b.progress < PREEMPT_TEST_MIN_PROGRESS)) {
        preemptive_task_test_fail("task-b-progress");
    }
    serial_write_string("  task-b-progress: PASS\n");

    if (task_stats_after.cooperative_yield_calls !=
        task_stats_before.cooperative_yield_calls) {
        preemptive_task_test_fail("no-cooperative-yield");
    }
    serial_write_string("  no-cooperative-yield: PASS\n");

    if ((task_stats_after.preemptions < PREEMPT_TEST_MIN_PREEMPTIONS) ||
        (task_stats_after.scheduler_ticks < PREEMPT_TEST_MIN_PREEMPTIONS) ||
        (task_a.slices < PREEMPT_TEST_MIN_SLICES) ||
        (task_b.slices < PREEMPT_TEST_MIN_SLICES) ||
        (task_a.resumes < (PREEMPT_TEST_MIN_SLICES - 1ULL)) ||
        (task_b.resumes < (PREEMPT_TEST_MIN_SLICES - 1ULL))) {
        preemptive_task_test_fail("repeated-preemption");
    }
    serial_write_string("  repeated-preemption: PASS\n");

    if ((!task_a.stack_ok) || (!task_b.stack_ok) ||
        (task_a.local_counter_address == 0U) ||
        (task_b.local_counter_address == 0U) ||
        (task_a.local_counter_address == task_b.local_counter_address) ||
        (task_a.checksum_address == task_b.checksum_address)) {
        preemptive_task_test_fail("stack-isolation");
    }
    serial_write_string("  stack-isolation: PASS\n");

    if ((!task_a.local_state_ok) || (!task_b.local_state_ok) ||
        (task_a.checksum == 0ULL) || (task_b.checksum == 0ULL)) {
        preemptive_task_test_fail("local-state");
    }
    serial_write_string("  local-state: PASS\n");

    if ((!task_a.register_ok) || (!task_b.register_ok) ||
        !x86_64_preemption_probe_is_armed()) {
        preemptive_task_test_fail("register-state");
    }
    serial_write_string("  register-state: PASS\n");

    if ((timer_ticks_during_test == 0ULL) ||
        (timer_irqs_during_test == 0ULL) ||
        (timer_ticks_during_test < task_stats_after.scheduler_ticks) ||
        (timer_irqs_during_test < task_stats_after.scheduler_ticks) ||
        (irq_after.unexpected_irq_count != irq_before.unexpected_irq_count)) {
        preemptive_task_test_fail("timer-delivery");
    }
    serial_write_string("  timer-delivery: PASS\n");

    if ((task_stats_after.current_task_id != KERNEL_BOOTSTRAP_TASK_ID) ||
        task_stats_after.preemption_enabled ||
        (task_stats_after.finished_tasks !=
         (task_stats_before.finished_tasks + 2ULL)) ||
        (task_stats_after.active_tasks != 2ULL)) {
        preemptive_task_test_fail("bootstrap-return");
    }
    serial_write_string("  bootstrap-return: PASS\n");

    if ((task_stats_after.finished_resume_count != 0ULL) ||
        (task_stats_after.scheduler_fault_count != 0ULL)) {
        preemptive_task_test_fail("finished-task-skip");
    }
    serial_write_string("  finished-task-skip: PASS\n");

    if (!task_finished_stacks_valid()) {
        preemptive_task_test_fail("stack-sentinel");
    }
    serial_write_string("  stack-sentinel: PASS\n");

    if (!task_cleanup_finished(&freed_stacks) ||
        (freed_stacks != 2ULL) ||
        !task_get_stats(&task_stats_cleanup) ||
        (task_stats_cleanup.active_tasks != 0ULL)) {
        preemptive_task_test_fail("stack-cleanup");
    }
    serial_write_string("  stack-cleanup: PASS\n");

    if (!heap_get_stats(&heap_after) ||
        (heap_after.allocation_count != heap_before.allocation_count) ||
        (heap_after.used_bytes != heap_before.used_bytes) ||
        (heap_after.mapped_pages < heap_before.mapped_pages) ||
        (task_stats_after.context_switches !=
         task_stats_before.context_switches)) {
        preemptive_task_test_fail("heap-bookkeeping");
    }
    serial_write_string("  heap-bookkeeping: PASS\n");

    serial_write_string("Timer ticks during test: ");
    serial_write_u64(timer_ticks_during_test);
    serial_write_string("\nScheduler ticks: ");
    serial_write_u64(task_stats_after.scheduler_ticks);
    serial_write_string("\nPreemptions: ");
    serial_write_u64(task_stats_after.preemptions);
    serial_write_string("\nTask A slices: ");
    serial_write_u64(task_a.slices);
    serial_write_string("\nTask B slices: ");
    serial_write_u64(task_b.slices);
    serial_write_string("\nTask A resumes: ");
    serial_write_u64(task_a.resumes);
    serial_write_string("\nTask B resumes: ");
    serial_write_u64(task_b.resumes);
    serial_write_string("\nCooperative yields during test: 0\n");
    serial_write_string("Task stacks freed: ");
    serial_write_u64(freed_stacks);
    serial_write_string("\nTask heap allocations after preemption cleanup: ");
    serial_write_u64(heap_after.allocation_count);
    serial_write_string("\n\nBoringKernel preemptive scheduling test passed.\n");
}
