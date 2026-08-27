#ifndef BORING_TASK_H
#define BORING_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_TASK_STACK_SIZE 16384U
#define KERNEL_TASK_MAX 8U
#define KERNEL_BOOTSTRAP_TASK_ID 0ULL

enum kernel_task_state {
    KERNEL_TASK_READY = 0,
    KERNEL_TASK_RUNNING = 1,
    KERNEL_TASK_FINISHED = 2,
    KERNEL_TASK_BLOCKED = 3
};

struct process;
struct x86_64_trap_frame;

struct task_stats {
    uint64_t created_tasks;
    uint64_t finished_tasks;
    uint64_t context_switches;
    uint64_t cooperative_yield_calls;
    uint64_t scheduler_ticks;
    uint64_t preemptions;
    uint64_t finished_resume_count;
    uint64_t scheduler_fault_count;
    uint64_t active_tasks;
    uint64_t current_task_id;
    uint64_t current_process_pid;
    size_t stack_size;
    bool preemption_enabled;
};

bool task_init(void);
bool task_create(void (*entry)(void *), void *arg, uint64_t *task_id);
bool task_create_for_process(struct process *process,
                             void (*entry)(void *),
                             void *arg, uint64_t *task_id);
bool task_create_preemptive(void (*entry)(void *), void *arg,
                            uint64_t *task_id);
bool task_create_preemptive_for_process(struct process *process,
                                        void (*entry)(void *),
                                        void *arg,
                                        uint64_t *task_id);
void task_yield(void);
bool task_block_current(void);
bool task_wake_process(struct process *process);
bool task_wake_pid(uint64_t pid);
void task_exit_current_process(void) __attribute__((noreturn));
uint64_t task_current_id(void);
uint64_t task_current_process_id(void);
uint64_t task_current_preempt_slices(void);
bool task_current_stack_contains(const void *address);
bool task_get_stats(struct task_stats *stats);
bool task_finished_stacks_valid(void);
bool task_cleanup_finished(uint64_t *freed_stacks);
bool task_preemption_start(void);
bool task_preemption_stop(void);
struct x86_64_trap_frame *task_scheduler_tick(
    struct x86_64_trap_frame *frame);

#endif
