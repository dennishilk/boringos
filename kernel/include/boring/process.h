#ifndef BORING_PROCESS_H
#define BORING_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/address_space.h>

#define KERNEL_BOOTSTRAP_PID 0ULL
#define KERNEL_PROCESS_MAX 4U

enum process_state {
    PROCESS_ALIVE = 0,
    PROCESS_FINISHED = 1
};

struct process {
    uint64_t pid;
    struct address_space address_space;
    enum process_state state;
    bool slot_used;
};

struct process_stats {
    uint64_t created_processes;
    uint64_t finished_processes;
    uint64_t active_processes;
    uint64_t current_pid;
};

bool process_init(void);
struct process *process_bootstrap(void);
struct process *process_current(void);
bool process_create(struct process **process_out);
bool process_activate(struct process *process);
bool process_mark_finished(struct process *process);
bool process_destroy(struct process *process);
bool process_is_alive(const struct process *process);
bool process_get_stats(struct process_stats *stats);

#endif
