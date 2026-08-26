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
    for (index = (size_t)KERNEL_FD_FIRST_REGULAR;
         index < (size_t)KERNEL_FD_MAX; ++index) {
        struct kernel_fd_slot *const slot = &table->slots[index];

        if (!slot->used) {
            continue;
        }
        if ((slot->kind != KERNEL_FD_REGULAR) || !slot->handle.open ||
            (vfs_handle_close(&slot->handle) != VFS_RESULT_OK)) {
            return false;
        }
        kernel_fd_slot_clear(slot);
    }
    for (index = 0U; index < (size_t)KERNEL_FD_FIRST_REGULAR; ++index) {
        kernel_fd_slot_clear(&table->slots[index]);
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
    if (fd < KERNEL_FD_FIRST_REGULAR) {
        return VFS_RESULT_ACCESS_DENIED;
    }
    slot = &table->slots[fd];
    if (!slot->used || (slot->kind != KERNEL_FD_REGULAR) ||
        !slot->handle.open) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    result = vfs_handle_close(&slot->handle);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    kernel_fd_slot_clear(slot);
    return VFS_RESULT_OK;
}
