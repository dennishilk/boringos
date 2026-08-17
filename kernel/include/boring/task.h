#ifndef BORING_TASK_H
#define BORING_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_TASK_STACK_SIZE 16384U
#define KERNEL_TASK_MAX 4U
#define KERNEL_BOOTSTRAP_TASK_ID 0ULL

enum kernel_task_state {
    KERNEL_TASK_READY = 0,
    KERNEL_TASK_RUNNING = 1,
    KERNEL_TASK_FINISHED = 2
};

struct task_stats {
    uint64_t created_tasks;
    uint64_t finished_tasks;
    uint64_t context_switches;
    uint64_t active_tasks;
    uint64_t current_task_id;
    size_t stack_size;
};

bool task_init(void);
bool task_create(void (*entry)(void *), void *arg, uint64_t *task_id);
void task_yield(void);
uint64_t task_current_id(void);
bool task_current_stack_contains(const void *address);
bool task_get_stats(struct task_stats *stats);
bool task_cleanup_finished(uint64_t *freed_stacks);

#endif
