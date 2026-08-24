#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/process.h>

static struct process bootstrap_process;
static struct process processes[KERNEL_PROCESS_MAX];
static struct process *current_process;
static uint64_t next_pid;
static uint64_t created_process_count;
static uint64_t finished_process_count;
static bool process_initialized;

static void process_clear(struct process *process) {
    size_t index;

    if (process == NULL) {
        return;
    }

    if (process->cwd_valid) {
        (void)vfs_path_release(&process->cwd);
    }
    process->pid = 0ULL;
    process->address_space.root_physical = 0ULL;
    process->address_space.owned_table_count = 0ULL;
    process->address_space.bootstrap = false;
    process->address_space.initialized = false;
    for (index = 0U;
         index < (size_t)ADDRESS_SPACE_MAX_OWNED_TABLE_FRAMES;
         ++index) {
        process->address_space.owned_table_frames[index] = 0ULL;
    }
    process->cwd.mount = NULL;
    process->cwd.node = NULL;
    process->state = PROCESS_FINISHED;
    process->cwd_valid = false;
    process->slot_used = false;
}

static bool process_is_regular(const struct process *process) {
    size_t index;

    if (process == NULL) {
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        if (process == &processes[index]) {
            return processes[index].slot_used;
        }
    }
    return false;
}

static bool process_is_known_alive(const struct process *process) {
    return (process != NULL) && process->slot_used &&
           (process->state == PROCESS_ALIVE) &&
           ((process == &bootstrap_process) || process_is_regular(process));
}

static void process_restore_interrupts(bool interrupts_were_enabled) {
    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
}

bool process_init(void) {
    size_t index;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if (process_initialized) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process_clear(&bootstrap_process);
    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        process_clear(&processes[index]);
    }

    if (!address_space_system_init(&bootstrap_process.address_space)) {
        process_clear(&bootstrap_process);
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    bootstrap_process.pid = KERNEL_BOOTSTRAP_PID;
    bootstrap_process.state = PROCESS_ALIVE;
    bootstrap_process.slot_used = true;
    current_process = &bootstrap_process;
    next_pid = 1ULL;
    created_process_count = 0ULL;
    finished_process_count = 0ULL;
    process_initialized = true;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

struct process *process_bootstrap(void) {
    if (!process_initialized) {
        return NULL;
    }
    return &bootstrap_process;
}

struct process *process_current(void) {
    if ((!process_initialized) || (current_process == NULL)) {
        return NULL;
    }
    return current_process;
}

bool process_create(struct process **process_out) {
    struct process *process = NULL;
    size_t index;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || (process_out == NULL) ||
        (next_pid == UINT64_MAX)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        if (!processes[index].slot_used) {
            process = &processes[index];
            break;
        }
    }
    if (process == NULL) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process_clear(process);
    if (!address_space_create(&process->address_space)) {
        process_clear(process);
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process->pid = next_pid;
    process->state = PROCESS_ALIVE;
    process->slot_used = true;
    ++next_pid;
    ++created_process_count;
    *process_out = process;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_activate(struct process *process) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || (process == NULL) ||
        !process->slot_used || (process->state != PROCESS_ALIVE) ||
        ((process != &bootstrap_process) && !process_is_regular(process)) ||
        !address_space_activate(&process->address_space)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    current_process = process;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_mark_finished(struct process *process) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_regular(process) ||
        (process->state != PROCESS_ALIVE) || (current_process == process)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process->state = PROCESS_FINISHED;
    ++finished_process_count;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_destroy(struct process *process) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_regular(process) ||
        (process->state != PROCESS_FINISHED) || (current_process == process) ||
        !address_space_destroy(&process->address_space)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process_clear(process);
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_is_alive(const struct process *process) {
    if ((!process_initialized) || !process_is_known_alive(process)) {
        return false;
    }
    return true;
}

bool process_set_cwd(struct process *process, const struct vfs_path *cwd) {
    struct vfs_path replacement = { NULL, NULL };
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_known_alive(process) ||
        !vfs_path_is_directory(cwd) ||
        (vfs_path_clone(cwd, &replacement) != VFS_RESULT_OK)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    if (process->cwd_valid &&
        (vfs_path_release(&process->cwd) != VFS_RESULT_OK)) {
        (void)vfs_path_release(&replacement);
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process->cwd = replacement;
    process->cwd_valid = true;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_clear_cwd(struct process *process) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_known_alive(process)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    if (!process->cwd_valid) {
        process_restore_interrupts(interrupts_were_enabled);
        return true;
    }
    if (vfs_path_release(&process->cwd) != VFS_RESULT_OK) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process->cwd_valid = false;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_get_cwd(const struct process *process, struct vfs_path *cwd_out) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    enum vfs_result result;

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_known_alive(process) ||
        !process->cwd_valid || (cwd_out == NULL)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    result = vfs_path_clone(&process->cwd, cwd_out);
    process_restore_interrupts(interrupts_were_enabled);
    return result == VFS_RESULT_OK;
}

bool process_get_stats(struct process_stats *stats) {
    uint64_t active = 0ULL;
    size_t index;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || (stats == NULL) ||
        (current_process == NULL)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        if (processes[index].slot_used) {
            ++active;
        }
    }

    stats->created_processes = created_process_count;
    stats->finished_processes = finished_process_count;
    stats->active_processes = active;
    stats->current_pid = current_process->pid;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}
