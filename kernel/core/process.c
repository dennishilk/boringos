#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/heap.h>
#include <boring/process.h>
#include <boring/pty.h>

static struct process bootstrap_process;
static struct process *process_registry_head;
static struct process *process_registry_tail;
static struct process *current_process;
static uint64_t live_process_count;
static uint64_t next_pid;
static uint64_t created_process_count;
static uint64_t finished_process_count;
static bool process_initialized;

static bool process_copy_text(char *destination,
                              size_t capacity,
                              const char *source) {
    size_t index;

    if ((destination == NULL) || (capacity == 0U) || (source == NULL)) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            return true;
        }
    }
    destination[capacity - 1U] = '\0';
    return false;
}

static void process_zero_text(char *text, size_t capacity) {
    size_t index;

    if (text == NULL) {
        return;
    }
    for (index = 0U; index < capacity; ++index) {
        text[index] = '\0';
    }
}

static void process_zero_object(struct process *process) {
    uint8_t *bytes;
    size_t index;

    if (process == NULL) {
        return;
    }
    bytes = (uint8_t *)process;
    for (index = 0U; index < sizeof(*process); ++index) {
        bytes[index] = 0U;
    }
}

static void process_clear(struct process *process) {
    size_t index;

    if (process == NULL) {
        return;
    }

    if (process->fd_table.initialized) {
        (void)kernel_fd_table_destroy(&process->fd_table);
    }
    if (process->cwd_valid) {
        (void)vfs_path_release(&process->cwd);
    }
    user_memory_process_state_init(&process->user_memory);
    process->pid = 0ULL;
    process->parent_pid = 0ULL;
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
    process_zero_text(process->name, sizeof(process->name));
    process_zero_text(process->username, sizeof(process->username));
    process_zero_text(process->cwd_text, sizeof(process->cwd_text));
    process->registry_prev = NULL;
    process->registry_next = NULL;
    process->cwd_valid = false;
    process->cwd_text_valid = false;
    process->slot_used = false;
}

static bool process_is_regular(const struct process *process) {
    const struct process *cursor;

    if (process == NULL) {
        return false;
    }
    for (cursor = process_registry_head; cursor != NULL;
         cursor = cursor->registry_next) {
        if (cursor == process) {
            return cursor->slot_used;
        }
    }
    return false;
}

static void process_registry_append(struct process *process) {
    process->registry_prev = process_registry_tail;
    process->registry_next = NULL;
    if (process_registry_tail != NULL) {
        process_registry_tail->registry_next = process;
    } else {
        process_registry_head = process;
    }
    process_registry_tail = process;
    ++live_process_count;
}

static void process_registry_remove(struct process *process) {
    if (process->registry_prev != NULL) {
        process->registry_prev->registry_next = process->registry_next;
    } else {
        process_registry_head = process->registry_next;
    }
    if (process->registry_next != NULL) {
        process->registry_next->registry_prev = process->registry_prev;
    } else {
        process_registry_tail = process->registry_prev;
    }
    process->registry_prev = NULL;
    process->registry_next = NULL;
    if (live_process_count != 0ULL) {
        --live_process_count;
    }
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
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if (process_initialized) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process_clear(&bootstrap_process);
    process_registry_head = NULL;
    process_registry_tail = NULL;
    live_process_count = 0ULL;
    if (!pty_init() || !user_memory_system_init() ||
        !address_space_system_init(&bootstrap_process.address_space)) {
        process_clear(&bootstrap_process);
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    bootstrap_process.pid = KERNEL_BOOTSTRAP_PID;
    bootstrap_process.parent_pid = KERNEL_BOOTSTRAP_PID;
    bootstrap_process.state = PROCESS_ALIVE;
    bootstrap_process.slot_used = true;
    (void)process_copy_text(bootstrap_process.name,
                            sizeof(bootstrap_process.name), "kernel");
    (void)process_copy_text(bootstrap_process.username,
                            sizeof(bootstrap_process.username), "kernel");
    current_process = &bootstrap_process;
    next_pid = 1ULL;
    created_process_count = 0ULL;
    finished_process_count = 0ULL;
    process_initialized = true;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

struct process *process_bootstrap(void) {
    return process_initialized ? &bootstrap_process : NULL;
}

struct process *process_current(void) {
    if ((!process_initialized) || (current_process == NULL)) {
        return NULL;
    }
    return current_process;
}

struct process *process_find_pid(uint64_t pid) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    struct process *found = NULL;
    struct process *cursor;

    x86_64_interrupts_disable();
    if (process_initialized) {
        if ((pid == KERNEL_BOOTSTRAP_PID) && bootstrap_process.slot_used) {
            found = &bootstrap_process;
        } else {
            for (cursor = process_registry_head; cursor != NULL;
                 cursor = cursor->registry_next) {
                if (cursor->slot_used && (cursor->pid == pid)) {
                    found = cursor;
                    break;
                }
            }
        }
    }
    process_restore_interrupts(interrupts_were_enabled);
    return found;
}

bool process_create(struct process **process_out) {
    struct process *process;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || (process_out == NULL) ||
        (next_pid == UINT64_MAX) ||
        (live_process_count >= (uint64_t)KERNEL_PROCESS_POLICY_LIMIT)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process = (struct process *)kmalloc(sizeof(*process));
    if (process == NULL) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process_zero_object(process);
    /*
     * Dynamic objects must enter the same initialized empty state that the
     * former static slots had after process_init().  In particular,
     * user_memory_process_state_init() seeds non-zero buffer-handle
     * generations; byte-zero alone is not a valid reusable process state.
     */
    process_clear(process);
    if (!address_space_create(&process->address_space)) {
        (void)kfree(process);
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    if (!kernel_fd_table_init(&process->fd_table)) {
        (void)address_space_destroy(&process->address_space);
        (void)kfree(process);
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }

    process->pid = next_pid;
    process->parent_pid =
        (current_process != NULL) ? current_process->pid : KERNEL_BOOTSTRAP_PID;
    process->state = PROCESS_ALIVE;
    process->slot_used = true;
    if (process->pid == 1ULL) {
        (void)process_copy_text(process->name, sizeof(process->name),
                                "boring-init");
        (void)process_copy_text(process->username, sizeof(process->username),
                                "boring");
    } else {
        (void)process_copy_text(process->name, sizeof(process->name),
                                "process");
        if ((current_process != NULL) &&
            (current_process != &bootstrap_process)) {
            (void)process_copy_text(process->username,
                                    sizeof(process->username),
                                    current_process->username);
        } else {
            (void)process_copy_text(process->username,
                                    sizeof(process->username), "boring");
        }
    }
    process_registry_append(process);
    ++next_pid;
    ++created_process_count;
    *process_out = process;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_discard_unstarted(struct process *process) {
    void *object;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_regular(process) ||
        (process->state != PROCESS_ALIVE) || (current_process == process) ||
        (process->pid == 0ULL) || (process->pid == UINT64_MAX) ||
        (next_pid != (process->pid + 1ULL)) ||
        (created_process_count == 0ULL) ||
        !user_memory_process_state_empty(&process->user_memory) ||
        !address_space_destroy(&process->address_space)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    next_pid = process->pid;
    --created_process_count;
    process_registry_remove(process);
    object = process;
    process_clear(process);
    if (!kfree(object)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_activate(struct process *process) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || (process == NULL) || !process->slot_used ||
        (process->state != PROCESS_ALIVE) ||
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
        (process->state != PROCESS_ALIVE) || (current_process == process) ||
        !user_memory_process_state_empty(&process->user_memory) ||
        !kernel_fd_table_destroy(&process->fd_table)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process->state = PROCESS_FINISHED;
    ++finished_process_count;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_destroy(struct process *process) {
    void *object;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_regular(process) ||
        (process->state != PROCESS_FINISHED) || (current_process == process) ||
        !user_memory_process_state_empty(&process->user_memory) ||
        !address_space_destroy(&process->address_space)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process_registry_remove(process);
    object = process;
    process_clear(process);
    if (!kfree(object)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_is_alive(const struct process *process) {
    return process_initialized && process_is_known_alive(process);
}

bool process_set_name(struct process *process, const char *name) {
    char replacement[KERNEL_PROCESS_NAME_MAX + 1U];
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    if (!process_copy_text(replacement, sizeof(replacement), name) ||
        (replacement[0] == '\0')) {
        return false;
    }
    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_known_alive(process)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    (void)process_copy_text(process->name, sizeof(process->name), replacement);
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

static bool process_replace_cwd(struct process *process,
                                const struct vfs_path *cwd,
                                const char *text) {
    struct vfs_path replacement = { NULL, NULL };

    if (!vfs_path_is_directory(cwd) ||
        (vfs_path_clone(cwd, &replacement) != VFS_RESULT_OK)) {
        return false;
    }
    if (process->cwd_valid &&
        (vfs_path_release(&process->cwd) != VFS_RESULT_OK)) {
        (void)vfs_path_release(&replacement);
        return false;
    }
    process->cwd = replacement;
    process->cwd_valid = true;
    process->cwd_text_valid = false;
    process_zero_text(process->cwd_text, sizeof(process->cwd_text));
    if (text != NULL) {
        if (!process_copy_text(process->cwd_text, sizeof(process->cwd_text),
                               text)) {
            (void)vfs_path_release(&process->cwd);
            process->cwd.mount = NULL;
            process->cwd.node = NULL;
            process->cwd_valid = false;
            return false;
        }
        process->cwd_text_valid = true;
    }
    return true;
}

bool process_set_cwd(struct process *process, const struct vfs_path *cwd) {
    const char *text = NULL;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_known_alive(process) ||
        (cwd == NULL)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    if ((cwd->node != NULL) && (cwd->node->parent == cwd->node)) {
        text = "/";
    } else if ((current_process != NULL) && (current_process != process) &&
               current_process->cwd_valid &&
               current_process->cwd_text_valid &&
               vfs_path_equal(cwd, &current_process->cwd)) {
        text = current_process->cwd_text;
    }
    if (!process_replace_cwd(process, cwd, text)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}

bool process_set_cwd_text(struct process *process,
                          const struct vfs_path *cwd,
                          const char *text) {
    char checked[VFS_PATH_MAX + 1U];
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    if (!process_copy_text(checked, sizeof(checked), text) ||
        (checked[0] != '/')) {
        return false;
    }
    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_known_alive(process) ||
        !process_replace_cwd(process, cwd, checked)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
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
    if (process->cwd_valid &&
        (vfs_path_release(&process->cwd) != VFS_RESULT_OK)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    process->cwd.mount = NULL;
    process->cwd.node = NULL;
    process->cwd_valid = false;
    process->cwd_text_valid = false;
    process_zero_text(process->cwd_text, sizeof(process->cwd_text));
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

bool process_get_cwd_text(const struct process *process,
                          char *buffer,
                          size_t capacity) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    bool copied;

    x86_64_interrupts_disable();
    if ((!process_initialized) || !process_is_known_alive(process) ||
        !process->cwd_text_valid || (buffer == NULL) || (capacity == 0U)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    copied = process_copy_text(buffer, capacity, process->cwd_text);
    process_restore_interrupts(interrupts_were_enabled);
    return copied;
}

bool process_get_snapshot(uint64_t index, struct process_snapshot *snapshot) {
    uint64_t logical = 0ULL;
    struct process *process;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || (snapshot == NULL)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    for (process = process_registry_head; process != NULL;
         process = process->registry_next) {
        if (!process->slot_used) {
            continue;
        }
        if (logical == index) {
            snapshot->pid = process->pid;
            snapshot->parent_pid = process->parent_pid;
            snapshot->state = (process->state == PROCESS_FINISHED) ?
                PROCESS_SNAPSHOT_ZOMBIE :
                ((process == current_process) ? PROCESS_SNAPSHOT_RUNNING :
                                                PROCESS_SNAPSHOT_WAITING);
            (void)process_copy_text(snapshot->name, sizeof(snapshot->name),
                                    process->name);
            (void)process_copy_text(snapshot->username,
                                    sizeof(snapshot->username),
                                    process->username);
            process_restore_interrupts(interrupts_were_enabled);
            return true;
        }
        ++logical;
    }
    process_restore_interrupts(interrupts_were_enabled);
    return false;
}

bool process_get_stats(struct process_stats *stats) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!process_initialized) || (stats == NULL) ||
        (current_process == NULL)) {
        process_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    stats->created_processes = created_process_count;
    stats->finished_processes = finished_process_count;
    stats->active_processes = live_process_count;
    stats->current_pid = current_process->pid;
    process_restore_interrupts(interrupts_were_enabled);
    return true;
}
