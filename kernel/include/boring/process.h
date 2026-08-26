#ifndef BORING_PROCESS_H
#define BORING_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/address_space.h>
#include <boring/fd.h>
#include <boring/user_memory.h>
#include <boring/vfs.h>

#define KERNEL_BOOTSTRAP_PID 0ULL
#define KERNEL_PROCESS_MAX 4U
#define KERNEL_PROCESS_NAME_MAX 31U
#define KERNEL_PROCESS_USER_MAX 31U

enum process_state {
    PROCESS_ALIVE = 0,
    PROCESS_FINISHED = 1
};

enum process_snapshot_state {
    PROCESS_SNAPSHOT_RUNNING = 1,
    PROCESS_SNAPSHOT_WAITING = 2,
    PROCESS_SNAPSHOT_ZOMBIE = 3
};

struct process {
    uint64_t pid;
    uint64_t parent_pid;
    struct address_space address_space;
    struct vfs_path cwd;
    struct kernel_fd_table fd_table;
    struct user_memory_process_state user_memory;
    enum process_state state;
    char name[KERNEL_PROCESS_NAME_MAX + 1U];
    char username[KERNEL_PROCESS_USER_MAX + 1U];
    char cwd_text[VFS_PATH_MAX + 1U];
    bool cwd_valid;
    bool cwd_text_valid;
    bool slot_used;
};

struct process_stats {
    uint64_t created_processes;
    uint64_t finished_processes;
    uint64_t active_processes;
    uint64_t current_pid;
};

struct process_snapshot {
    uint64_t pid;
    uint64_t parent_pid;
    enum process_snapshot_state state;
    char name[KERNEL_PROCESS_NAME_MAX + 1U];
    char username[KERNEL_PROCESS_USER_MAX + 1U];
};

bool process_init(void);
struct process *process_bootstrap(void);
struct process *process_current(void);
bool process_create(struct process **process_out);
bool process_discard_unstarted(struct process *process);
bool process_activate(struct process *process);
bool process_mark_finished(struct process *process);
bool process_destroy(struct process *process);
bool process_is_alive(const struct process *process);
bool process_set_name(struct process *process, const char *name);
bool process_set_cwd(struct process *process, const struct vfs_path *cwd);
bool process_set_cwd_text(struct process *process,
                          const struct vfs_path *cwd,
                          const char *text);
bool process_clear_cwd(struct process *process);
bool process_get_cwd(const struct process *process, struct vfs_path *cwd_out);
bool process_get_cwd_text(const struct process *process,
                          char *buffer,
                          size_t capacity);
bool process_get_snapshot(uint64_t index, struct process_snapshot *snapshot);
bool process_get_stats(struct process_stats *stats);

#endif
