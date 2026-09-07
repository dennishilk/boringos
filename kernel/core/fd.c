#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/fd.h>

static void kernel_fd_handle_clear(struct vfs_handle *handle) {
    if (handle == NULL) {
        return;
    }
    handle->path.mount = NULL;
    handle->path.node = NULL;
    handle->offset = 0ULL;
    handle->access = 0U;
    handle->open = false;
}

static void kernel_fd_slot_clear(struct kernel_fd_slot *slot) {
    if (slot == NULL) {
        return;
    }
    kernel_fd_handle_clear(&slot->handle);
    slot->pty.slot = 0U;
    slot->pty.endpoint = 0U;
    slot->pty.generation = 0U;
    slot->kind = KERNEL_FD_UNUSED;
    slot->access = 0U;
    slot->used = false;
}

static bool kernel_fd_access_valid(uint32_t access) {
    return (access != 0U) && ((access & ~VFS_ACCESS_MASK) == 0U);
}

bool kernel_fd_table_init(struct kernel_fd_table *table) {
    size_t index;

    if ((table == NULL) || table->initialized) {
        return false;
    }
    for (index = 0U; index < (size_t)KERNEL_FD_MAX; ++index) {
        kernel_fd_slot_clear(&table->slots[index]);
    }
    table->slots[KERNEL_FD_STDIN].kind = KERNEL_FD_CONSOLE_INPUT;
    table->slots[KERNEL_FD_STDIN].access = VFS_ACCESS_READ;
    table->slots[KERNEL_FD_STDIN].used = true;
    table->slots[KERNEL_FD_STDOUT].kind = KERNEL_FD_CONSOLE_OUTPUT;
    table->slots[KERNEL_FD_STDOUT].access = VFS_ACCESS_WRITE;
    table->slots[KERNEL_FD_STDOUT].used = true;
    table->slots[KERNEL_FD_STDERR].kind = KERNEL_FD_CONSOLE_OUTPUT;
    table->slots[KERNEL_FD_STDERR].access = VFS_ACCESS_WRITE;
    table->slots[KERNEL_FD_STDERR].used = true;
    table->initialized = true;
    return true;
}

bool kernel_fd_table_destroy(struct kernel_fd_table *table) {
    size_t index;

    if (table == NULL) {
        return false;
    }
    if (!table->initialized) {
        return true;
    }
    for (index = 0U; index < (size_t)KERNEL_FD_MAX; ++index) {
        struct kernel_fd_slot *const slot = &table->slots[index];

        if (!slot->used) {
            continue;
        }
        if (slot->kind == KERNEL_FD_REGULAR) {
            if ((index < (size_t)KERNEL_FD_FIRST_REGULAR) || !slot->handle.open ||
                (vfs_handle_close(&slot->handle) != VFS_RESULT_OK)) {
                return false;
            }
        } else if (slot->kind == KERNEL_FD_PTY) {
            if (pty_close(slot->pty) != PTY_RESULT_OK) {
                return false;
            }
        } else if (((index == (size_t)KERNEL_FD_STDIN) &&
                    (slot->kind == KERNEL_FD_CONSOLE_INPUT)) ||
                   (((index == (size_t)KERNEL_FD_STDOUT) ||
                     (index == (size_t)KERNEL_FD_STDERR)) &&
                    (slot->kind == KERNEL_FD_CONSOLE_OUTPUT))) {
            /* Bootstrap console descriptors do not own an external object. */
        } else {
            return false;
        }
        kernel_fd_slot_clear(slot);
    }
    table->initialized = false;
    return true;
}

bool kernel_fd_describe(const struct kernel_fd_table *table,
                        uint32_t fd,
                        enum kernel_fd_kind *kind_out,
                        uint32_t *access_out) {
    const struct kernel_fd_slot *slot;

    if ((table == NULL) || !table->initialized ||
        (fd >= KERNEL_FD_MAX) || (kind_out == NULL) ||
        (access_out == NULL)) {
        return false;
    }
    slot = &table->slots[fd];
    if (!slot->used || (slot->kind == KERNEL_FD_UNUSED) ||
        !kernel_fd_access_valid(slot->access)) {
        return false;
    }
    *kind_out = slot->kind;
    *access_out = slot->access;
    return true;
}

enum vfs_result kernel_fd_open_regular(struct kernel_fd_table *table,
                                       const struct vfs_path *path,
                                       uint32_t access,
                                       uint32_t *fd_out) {
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    size_t index;
    enum vfs_result result;

    if ((table == NULL) || !table->initialized || (path == NULL) ||
        (path->node == NULL) || (fd_out == NULL) ||
        !kernel_fd_access_valid(access)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (path->node->type != VFS_NODE_REGULAR) {
        return VFS_RESULT_NOT_REGULAR;
    }
    for (index = (size_t)KERNEL_FD_FIRST_REGULAR;
         index < (size_t)KERNEL_FD_MAX; ++index) {
        if (!table->slots[index].used) {
            break;
        }
    }
    if (index == (size_t)KERNEL_FD_MAX) {
        return VFS_RESULT_NO_SPACE;
    }
    result = vfs_handle_open(path, access, &handle);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    table->slots[index].handle = handle;
    table->slots[index].kind = KERNEL_FD_REGULAR;
    table->slots[index].access = access;
    table->slots[index].used = true;
    *fd_out = (uint32_t)index;
    return VFS_RESULT_OK;
}

static enum vfs_result kernel_fd_regular_slot(
    struct kernel_fd_table *table,
    uint32_t fd,
    uint32_t required_access,
    struct kernel_fd_slot **slot_out) {
    struct kernel_fd_slot *slot;

    if ((table == NULL) || !table->initialized ||
        (fd < KERNEL_FD_FIRST_REGULAR) || (fd >= KERNEL_FD_MAX) ||
        (slot_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    slot = &table->slots[fd];
    if (!slot->used || (slot->kind != KERNEL_FD_REGULAR) ||
        !slot->handle.open) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if ((slot->access & required_access) == 0U) {
        return VFS_RESULT_ACCESS_DENIED;
    }
    *slot_out = slot;
    return VFS_RESULT_OK;
}

enum vfs_result kernel_fd_read_regular(struct kernel_fd_table *table,
                                       uint32_t fd,
                                       void *buffer,
                                       size_t length,
                                       size_t *transferred_out) {
    struct kernel_fd_slot *slot;
    enum vfs_result result;

    if (transferred_out != NULL) {
        *transferred_out = 0U;
    }
    if (((buffer == NULL) && (length != 0U)) ||
        (length > (size_t)VFS_IO_MAX) || (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    result = kernel_fd_regular_slot(table, fd, VFS_ACCESS_READ, &slot);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    return vfs_handle_read(&slot->handle, buffer, length, transferred_out);
}

enum vfs_result kernel_fd_write_regular(struct kernel_fd_table *table,
                                        uint32_t fd,
                                        const void *buffer,
                                        size_t length,
                                        size_t *transferred_out) {
    struct kernel_fd_slot *slot;
    enum vfs_result result;

    if (transferred_out != NULL) {
        *transferred_out = 0U;
    }
    if (((buffer == NULL) && (length != 0U)) ||
        (length > (size_t)VFS_IO_MAX) || (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    result = kernel_fd_regular_slot(table, fd, VFS_ACCESS_WRITE, &slot);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    return vfs_handle_write(&slot->handle, buffer, length, transferred_out);
}


static enum vfs_result kernel_fd_free_slot(struct kernel_fd_table *table,
                                           uint32_t first,
                                           uint32_t *fd_out) {
    uint32_t fd;
    if ((table == NULL) || !table->initialized || (fd_out == NULL) ||
        (first >= KERNEL_FD_MAX)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    for (fd = first; fd < KERNEL_FD_MAX; ++fd) {
        if (!table->slots[fd].used) {
            *fd_out = fd;
            return VFS_RESULT_OK;
        }
    }
    return VFS_RESULT_NO_SPACE;
}

enum vfs_result kernel_fd_bind_pty(struct kernel_fd_table *table,
                                   uint32_t fd,
                                   struct pty_handle handle,
                                   uint32_t access) {
    struct kernel_fd_slot *slot;
    if ((table == NULL) || !table->initialized || (fd >= KERNEL_FD_MAX) ||
        !kernel_fd_access_valid(access)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    slot = &table->slots[fd];
    if (slot->used) {
        if (slot->kind == KERNEL_FD_REGULAR) {
            if (!slot->handle.open ||
                (vfs_handle_close(&slot->handle) != VFS_RESULT_OK)) {
                return VFS_RESULT_CORRUPT;
            }
        } else if (slot->kind == KERNEL_FD_PTY) {
            if (pty_close(slot->pty) != PTY_RESULT_OK) {
                return VFS_RESULT_CORRUPT;
            }
        } else if ((slot->kind != KERNEL_FD_CONSOLE_INPUT) &&
                   (slot->kind != KERNEL_FD_CONSOLE_OUTPUT)) {
            return VFS_RESULT_CORRUPT;
        }
    }
    kernel_fd_slot_clear(slot);
    if (pty_retain(handle) != PTY_RESULT_OK) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    slot->pty = handle;
    slot->kind = KERNEL_FD_PTY;
    slot->access = access;
    slot->used = true;
    return VFS_RESULT_OK;
}

enum vfs_result kernel_fd_clone_stdio(
    const struct kernel_fd_table *source_table, uint32_t source_fd,
    struct kernel_fd_table *target_table, uint32_t target_fd,
    uint32_t required_access) {
    const struct kernel_fd_slot *source;
    enum kernel_fd_kind expected_console;

    if ((source_table == NULL) || !source_table->initialized ||
        (target_table == NULL) || !target_table->initialized ||
        (source_fd >= KERNEL_FD_MAX) ||
        (target_fd >= KERNEL_FD_FIRST_REGULAR) ||
        !kernel_fd_access_valid(required_access)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    source = &source_table->slots[source_fd];
    if (!source->used ||
        ((source->access & required_access) != required_access)) {
        return VFS_RESULT_ACCESS_DENIED;
    }
    if (source->kind == KERNEL_FD_PTY) {
        return kernel_fd_bind_pty(target_table, target_fd, source->pty,
                                  required_access);
    }
    expected_console = (target_fd == KERNEL_FD_STDIN) ?
        KERNEL_FD_CONSOLE_INPUT : KERNEL_FD_CONSOLE_OUTPUT;
    if ((source->kind != expected_console) ||
        !target_table->slots[target_fd].used ||
        (target_table->slots[target_fd].kind != expected_console)) {
        return VFS_RESULT_ACCESS_DENIED;
    }
    return VFS_RESULT_OK;
}

enum vfs_result kernel_fd_install_pty(struct kernel_fd_table *table,
                                      struct pty_handle handle,
                                      uint32_t access,
                                      uint32_t *fd_out) {
    uint32_t fd;
    enum vfs_result result = kernel_fd_free_slot(
        table, KERNEL_FD_FIRST_REGULAR, &fd);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = kernel_fd_bind_pty(table, fd, handle, access);
    if (result == VFS_RESULT_OK) {
        *fd_out = fd;
    }
    return result;
}

static struct kernel_fd_slot *kernel_fd_pty_slot(struct kernel_fd_table *table,
                                                  uint32_t fd,
                                                  uint32_t access) {
    struct kernel_fd_slot *slot;
    if ((table == NULL) || !table->initialized || (fd >= KERNEL_FD_MAX)) {
        return NULL;
    }
    slot = &table->slots[fd];
    if (!slot->used || (slot->kind != KERNEL_FD_PTY) ||
        ((slot->access & access) == 0U)) {
        return NULL;
    }
    return slot;
}

enum pty_result kernel_fd_read_pty(struct kernel_fd_table *table,
                                   uint32_t fd, void *buffer, size_t length,
                                   size_t *transferred_out) {
    struct kernel_fd_slot *slot = kernel_fd_pty_slot(table, fd, VFS_ACCESS_READ);
    return (slot == NULL) ? PTY_RESULT_INVALID :
        pty_read(slot->pty, buffer, length, transferred_out);
}

enum pty_result kernel_fd_write_pty(struct kernel_fd_table *table,
                                    uint32_t fd, const void *buffer,
                                    size_t length, size_t *transferred_out) {
    struct kernel_fd_slot *slot = kernel_fd_pty_slot(table, fd, VFS_ACCESS_WRITE);
    return (slot == NULL) ? PTY_RESULT_INVALID :
        pty_write(slot->pty, buffer, length, transferred_out);
}

enum pty_result kernel_fd_poll_pty(const struct kernel_fd_table *table,
                                   uint32_t fd,
                                   struct pty_poll_state *state_out) {
    const struct kernel_fd_slot *slot;
    if ((table == NULL) || !table->initialized || (fd >= KERNEL_FD_MAX)) {
        return PTY_RESULT_INVALID;
    }
    slot = &table->slots[fd];
    if (!slot->used || (slot->kind != KERNEL_FD_PTY) ||
        ((slot->access & VFS_ACCESS_READ) == 0U)) {
        return PTY_RESULT_INVALID;
    }
    return pty_poll(slot->pty, state_out);
}

enum pty_result kernel_fd_arm_pty_waiter(const struct kernel_fd_table *table,
                                         uint32_t fd, uint64_t pid) {
    const struct kernel_fd_slot *slot;
    if ((table == NULL) || !table->initialized || (fd >= KERNEL_FD_MAX)) {
        return PTY_RESULT_INVALID;
    }
    slot = &table->slots[fd];
    if (!slot->used || (slot->kind != KERNEL_FD_PTY) ||
        ((slot->access & VFS_ACCESS_READ) == 0U)) {
        return PTY_RESULT_INVALID;
    }
    return pty_arm_read_waiter(slot->pty, pid);
}

void kernel_fd_cancel_pty_waiter(const struct kernel_fd_table *table,
                                 uint32_t fd, uint64_t pid) {
    const struct kernel_fd_slot *slot;
    if ((table == NULL) || !table->initialized || (fd >= KERNEL_FD_MAX)) {
        return;
    }
    slot = &table->slots[fd];
    if (slot->used && (slot->kind == KERNEL_FD_PTY)) {
        pty_cancel_read_waiter(slot->pty, pid);
    }
}

enum vfs_result kernel_fd_get_offset(const struct kernel_fd_table *table,
                                     uint32_t fd,
                                     uint64_t *offset_out) {
    const struct kernel_fd_slot *slot;

    if ((table == NULL) || !table->initialized ||
        (fd < KERNEL_FD_FIRST_REGULAR) || (fd >= KERNEL_FD_MAX) ||
        (offset_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    slot = &table->slots[fd];
    if (!slot->used || (slot->kind != KERNEL_FD_REGULAR) ||
        !slot->handle.open) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    return vfs_handle_get_offset(&slot->handle, offset_out);
}

enum vfs_result kernel_fd_close(struct kernel_fd_table *table, uint32_t fd) {
    struct kernel_fd_slot *slot;
    enum vfs_result result;

    if ((table == NULL) || !table->initialized || (fd >= KERNEL_FD_MAX)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    slot = &table->slots[fd];
    if (!slot->used) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (slot->kind == KERNEL_FD_REGULAR) {
        if ((fd < KERNEL_FD_FIRST_REGULAR) || !slot->handle.open) {
            return VFS_RESULT_INVALID_ARGUMENT;
        }
        result = vfs_handle_close(&slot->handle);
        if (result != VFS_RESULT_OK) {
            return result;
        }
    } else if (slot->kind == KERNEL_FD_PTY) {
        if (pty_close(slot->pty) != PTY_RESULT_OK) {
            return VFS_RESULT_INVALID_ARGUMENT;
        }
    } else {
        return VFS_RESULT_ACCESS_DENIED;
    }
    kernel_fd_slot_clear(slot);
    return VFS_RESULT_OK;
}
