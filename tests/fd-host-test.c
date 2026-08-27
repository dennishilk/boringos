#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <boring/fd.h>

static const uint8_t backing[] = { 'a', 'b', 'c', 'd', 'e', 'f' };
static uint64_t open_count;
static uint64_t close_count;
static struct vfs_filesystem fake_filesystem;
static struct vfs_node regular_node;
static struct vfs_node directory_node;
static struct vfs_path regular_path;
static struct vfs_path directory_path;

bool task_wake_pid(uint64_t pid);
bool task_wake_pid(uint64_t pid) {
    (void)pid;
    return true;
}

static int fail(const char *message) {
    (void)fprintf(stderr, "fd-host-test: %s\n", message);
    return 1;
}

enum vfs_result vfs_handle_open(const struct vfs_path *path,
                                uint32_t access,
                                struct vfs_handle *handle_out) {
    if ((path == NULL) || (path->node == NULL) || (handle_out == NULL) ||
        (access == 0U) || ((access & ~VFS_ACCESS_MASK) != 0U)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (path->node->type != VFS_NODE_REGULAR) {
        return VFS_RESULT_NOT_REGULAR;
    }
    handle_out->path = *path;
    handle_out->offset = 0ULL;
    handle_out->access = access;
    handle_out->open = true;
    ++open_count;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_read(struct vfs_handle *handle,
                                void *buffer,
                                size_t length,
                                size_t *transferred_out) {
    uint8_t *output = (uint8_t *)buffer;
    size_t available;
    size_t transfer;
    size_t index;

    if ((handle == NULL) || !handle->open ||
        ((handle->access & VFS_ACCESS_READ) == 0U) ||
        ((buffer == NULL) && (length != 0U)) || (transferred_out == NULL) ||
        (handle->offset > (uint64_t)sizeof(backing))) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    available = sizeof(backing) - (size_t)handle->offset;
    transfer = (length < available) ? length : available;
    for (index = 0U; index < transfer; ++index) {
        output[index] = backing[(size_t)handle->offset + index];
    }
    handle->offset += (uint64_t)transfer;
    *transferred_out = transfer;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_write(struct vfs_handle *handle,
                                 const void *buffer,
                                 size_t length,
                                 size_t *transferred_out) {
    if ((handle == NULL) || !handle->open ||
        ((handle->access & VFS_ACCESS_WRITE) == 0U) ||
        ((buffer == NULL) && (length != 0U)) || (transferred_out == NULL) ||
        (handle->offset > UINT64_MAX - (uint64_t)length)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    handle->offset += (uint64_t)length;
    *transferred_out = length;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_get_offset(const struct vfs_handle *handle,
                                      uint64_t *offset_out) {
    if ((handle == NULL) || !handle->open || (offset_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *offset_out = handle->offset;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_close(struct vfs_handle *handle) {
    if ((handle == NULL) || !handle->open) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    handle->path.mount = NULL;
    handle->path.node = NULL;
    handle->offset = 0ULL;
    handle->access = 0U;
    handle->open = false;
    ++close_count;
    return VFS_RESULT_OK;
}

static bool standard_descriptors_ok(const struct kernel_fd_table *table) {
    enum kernel_fd_kind kind;
    uint32_t access;

    return kernel_fd_describe(table, KERNEL_FD_STDIN, &kind, &access) &&
           (kind == KERNEL_FD_CONSOLE_INPUT) &&
           (access == VFS_ACCESS_READ) &&
           kernel_fd_describe(table, KERNEL_FD_STDOUT, &kind, &access) &&
           (kind == KERNEL_FD_CONSOLE_OUTPUT) &&
           (access == VFS_ACCESS_WRITE) &&
           kernel_fd_describe(table, KERNEL_FD_STDERR, &kind, &access) &&
           (kind == KERNEL_FD_CONSOLE_OUTPUT) &&
           (access == VFS_ACCESS_WRITE);
}

int main(void) {
    struct kernel_fd_table parent = { 0 };
    struct kernel_fd_table child = { 0 };
    uint32_t fd_a;
    uint32_t fd_b;
    uint32_t fd_reused;
    uint32_t fd_write_only;
    uint32_t fd_read_only;
    uint32_t fd;
    enum kernel_fd_kind kind;
    uint32_t access;
    uint8_t buffer[3] = { 0U, 0U, 0U };
    size_t transferred;
    uint64_t offset_a;
    uint64_t offset_b;
    uint64_t closes_before_second_destroy;
    enum vfs_result result;

    (void)memset(&fake_filesystem, 0, sizeof(fake_filesystem));
    (void)memset(&regular_node, 0, sizeof(regular_node));
    (void)memset(&directory_node, 0, sizeof(directory_node));
    regular_node.type = VFS_NODE_REGULAR;
    regular_node.filesystem = &fake_filesystem;
    regular_node.valid = true;
    directory_node.type = VFS_NODE_DIRECTORY;
    directory_node.filesystem = &fake_filesystem;
    directory_node.valid = true;
    regular_path.mount = NULL;
    regular_path.node = &regular_node;
    directory_path.mount = NULL;
    directory_path.node = &directory_node;

    if (!kernel_fd_table_init(&parent) || !standard_descriptors_ok(&parent)) {
        return fail("standard descriptors were not initialized exactly");
    }
    if (kernel_fd_open_regular(&parent, &regular_path, VFS_ACCESS_READ,
                               &fd_a) != VFS_RESULT_OK ||
        (fd_a != KERNEL_FD_FIRST_REGULAR)) {
        return fail("first regular descriptor was not fd 3");
    }
    if (kernel_fd_open_regular(&parent, &regular_path, VFS_ACCESS_READ,
                               &fd_b) != VFS_RESULT_OK ||
        (fd_b == fd_a)) {
        return fail("second descriptor allocation failed");
    }
    if (kernel_fd_read_regular(&parent, fd_a, buffer, 2U, &transferred) !=
            VFS_RESULT_OK ||
        (transferred != 2U) || (buffer[0] != 'a') || (buffer[1] != 'b') ||
        kernel_fd_read_regular(&parent, fd_b, buffer, 2U, &transferred) !=
            VFS_RESULT_OK ||
        (transferred != 2U) || (buffer[0] != 'a') || (buffer[1] != 'b') ||
        kernel_fd_read_regular(&parent, fd_a, buffer, 2U, &transferred) !=
            VFS_RESULT_OK ||
        (transferred != 2U) || (buffer[0] != 'c') || (buffer[1] != 'd') ||
        kernel_fd_get_offset(&parent, fd_a, &offset_a) != VFS_RESULT_OK ||
        kernel_fd_get_offset(&parent, fd_b, &offset_b) != VFS_RESULT_OK ||
        (offset_a != 4ULL) || (offset_b != 2ULL)) {
        return fail("separately opened files did not keep independent offsets");
    }
    if (kernel_fd_close(&parent, fd_a) != VFS_RESULT_OK ||
        kernel_fd_close(&parent, fd_a) != VFS_RESULT_INVALID_ARGUMENT ||
        kernel_fd_open_regular(&parent, &regular_path, VFS_ACCESS_READ,
                               &fd_reused) != VFS_RESULT_OK ||
        (fd_reused != fd_a)) {
        return fail("close or descriptor slot reuse failed");
    }
    if (kernel_fd_close(&parent, KERNEL_FD_STDIN) !=
            VFS_RESULT_ACCESS_DENIED ||
        kernel_fd_close(&parent, KERNEL_FD_MAX) !=
            VFS_RESULT_INVALID_ARGUMENT ||
        kernel_fd_describe(&parent, KERNEL_FD_MAX, &kind, &access)) {
        return fail("invalid or standard descriptor close contract failed");
    }
    if (kernel_fd_open_regular(&parent, &directory_path, VFS_ACCESS_READ,
                               &fd) != VFS_RESULT_NOT_REGULAR) {
        return fail("directory was accepted as a regular descriptor");
    }
    if (kernel_fd_open_regular(&parent, &regular_path, VFS_ACCESS_WRITE,
                               &fd_write_only) != VFS_RESULT_OK ||
        kernel_fd_read_regular(&parent, fd_write_only, buffer, 1U,
                               &transferred) != VFS_RESULT_ACCESS_DENIED ||
        kernel_fd_open_regular(&parent, &regular_path, VFS_ACCESS_READ,
                               &fd_read_only) != VFS_RESULT_OK ||
        kernel_fd_write_regular(&parent, fd_read_only, buffer, 1U,
                                &transferred) != VFS_RESULT_ACCESS_DENIED) {
        return fail("descriptor access-mode validation failed");
    }

    for (;;) {
        result = kernel_fd_open_regular(&parent, &regular_path,
                                        VFS_ACCESS_READ, &fd);
        if (result == VFS_RESULT_NO_SPACE) {
            break;
        }
        if ((result != VFS_RESULT_OK) || (fd < KERNEL_FD_FIRST_REGULAR) ||
            (fd >= KERNEL_FD_MAX)) {
            return fail("descriptor table fill failed");
        }
    }
    if (kernel_fd_open_regular(&parent, &regular_path, VFS_ACCESS_READ,
                               &fd) != VFS_RESULT_NO_SPACE) {
        return fail("full descriptor table was not rejected");
    }

    if (!kernel_fd_table_init(&child) || !standard_descriptors_ok(&child) ||
        kernel_fd_describe(&child, KERNEL_FD_FIRST_REGULAR, &kind, &access)) {
        return fail("child table inherited a parent regular descriptor");
    }
    if (!kernel_fd_table_destroy(&child)) {
        return fail("fresh child descriptor teardown failed");
    }

    if (!kernel_fd_table_destroy(&parent) || (open_count != close_count)) {
        return fail("process teardown leaked retained file handles");
    }
    closes_before_second_destroy = close_count;
    if (!kernel_fd_table_destroy(&parent) ||
        (close_count != closes_before_second_destroy)) {
        return fail("descriptor teardown double-closed a handle");
    }

    (void)puts("fd-host-test: standard descriptors, bounds, slot reuse, independent offsets, access modes, child isolation and teardown passed.");
    return 0;
}
