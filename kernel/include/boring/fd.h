#ifndef BORING_FD_H
#define BORING_FD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/pty.h>
#include <boring/vfs.h>

#define KERNEL_FD_MAX 16U
#define KERNEL_FD_STDIN 0U
#define KERNEL_FD_STDOUT 1U
#define KERNEL_FD_STDERR 2U
#define KERNEL_FD_FIRST_REGULAR 3U

enum kernel_fd_kind {
    KERNEL_FD_UNUSED = 0,
    KERNEL_FD_CONSOLE_INPUT = 1,
    KERNEL_FD_CONSOLE_OUTPUT = 2,
    KERNEL_FD_REGULAR = 3,
    KERNEL_FD_PTY = 4
};

struct kernel_fd_slot {
    struct vfs_handle handle;
    struct pty_handle pty;
    enum kernel_fd_kind kind;
    uint32_t access;
    bool used;
};

struct kernel_fd_table {
    struct kernel_fd_slot slots[KERNEL_FD_MAX];
    bool initialized;
};

bool kernel_fd_table_init(struct kernel_fd_table *table);
bool kernel_fd_table_destroy(struct kernel_fd_table *table);
bool kernel_fd_describe(const struct kernel_fd_table *table,
                        uint32_t fd,
                        enum kernel_fd_kind *kind_out,
                        uint32_t *access_out);
enum vfs_result kernel_fd_open_regular(struct kernel_fd_table *table,
                                       const struct vfs_path *path,
                                       uint32_t access,
                                       uint32_t *fd_out);
enum vfs_result kernel_fd_read_regular(struct kernel_fd_table *table,
                                       uint32_t fd,
                                       void *buffer,
                                       size_t length,
                                       size_t *transferred_out);
enum vfs_result kernel_fd_write_regular(struct kernel_fd_table *table,
                                        uint32_t fd,
                                        const void *buffer,
                                        size_t length,
                                        size_t *transferred_out);
enum vfs_result kernel_fd_get_offset(const struct kernel_fd_table *table,
                                     uint32_t fd,
                                     uint64_t *offset_out);

enum vfs_result kernel_fd_install_pty(struct kernel_fd_table *table,
                                      struct pty_handle handle,
                                      uint32_t access,
                                      uint32_t *fd_out);
enum vfs_result kernel_fd_bind_pty(struct kernel_fd_table *table,
                                   uint32_t fd,
                                   struct pty_handle handle,
                                   uint32_t access);
enum vfs_result kernel_fd_clone_stdio(
    const struct kernel_fd_table *source_table, uint32_t source_fd,
    struct kernel_fd_table *target_table, uint32_t target_fd,
    uint32_t required_access);
enum pty_result kernel_fd_read_pty(struct kernel_fd_table *table,
                                   uint32_t fd, void *buffer, size_t length,
                                   size_t *transferred_out);
enum pty_result kernel_fd_write_pty(struct kernel_fd_table *table,
                                    uint32_t fd, const void *buffer,
                                    size_t length, size_t *transferred_out);
enum pty_result kernel_fd_poll_pty(const struct kernel_fd_table *table,
                                   uint32_t fd,
                                   struct pty_poll_state *state_out);
enum pty_result kernel_fd_arm_pty_waiter(const struct kernel_fd_table *table,
                                         uint32_t fd, uint64_t pid);
void kernel_fd_cancel_pty_waiter(const struct kernel_fd_table *table,
                                 uint32_t fd, uint64_t pid);
enum vfs_result kernel_fd_close(struct kernel_fd_table *table, uint32_t fd);

#endif
