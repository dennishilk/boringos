#ifndef BORING_VFS_H
#define BORING_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VFS_PATH_MAX 1024U
#define VFS_NAME_MAX 255U
#define VFS_IO_MAX 4096U
#define VFS_MOUNT_MAX 8U

#define VFS_ACCESS_READ (1U << 0)
#define VFS_ACCESS_WRITE (1U << 1)
#define VFS_ACCESS_MASK (VFS_ACCESS_READ | VFS_ACCESS_WRITE)

enum vfs_result {
    VFS_RESULT_OK = 0,
    VFS_RESULT_INVALID_ARGUMENT,
    VFS_RESULT_NOT_INITIALIZED,
    VFS_RESULT_ALREADY_INITIALIZED,
    VFS_RESULT_NOT_FOUND,
    VFS_RESULT_NOT_DIRECTORY,
    VFS_RESULT_NOT_REGULAR,
    VFS_RESULT_PATH_TOO_LONG,
    VFS_RESULT_NAME_TOO_LONG,
    VFS_RESULT_EMPTY_PATH,
    VFS_RESULT_ALREADY_MOUNTED,
    VFS_RESULT_MOUNT_CONFLICT,
    VFS_RESULT_CROSS_FILESYSTEM,
    VFS_RESULT_NOT_SUPPORTED,
    VFS_RESULT_OVERFLOW,
    VFS_RESULT_BUSY,
    VFS_RESULT_CORRUPT,
    VFS_RESULT_NO_CWD,
    VFS_RESULT_ACCESS_DENIED,
    VFS_RESULT_NO_SPACE
};

enum vfs_node_type {
    VFS_NODE_DIRECTORY = 1,
    VFS_NODE_REGULAR = 2
};

struct process;
struct vfs_filesystem;
struct vfs_mount;
struct vfs_node;

struct vfs_dirent {
    uint64_t node_id;
    enum vfs_node_type type;
    size_t name_length;
    char name[VFS_NAME_MAX + 1U];
};

struct vfs_operations {
    enum vfs_result (*lookup)(struct vfs_filesystem *filesystem,
                              struct vfs_node *directory,
                              const char *name,
                              size_t name_length,
                              struct vfs_node **node_out);
    enum vfs_result (*create)(struct vfs_filesystem *filesystem,
                              struct vfs_node *directory,
                              const char *name,
                              size_t name_length,
                              struct vfs_node **node_out);
    enum vfs_result (*mkdir)(struct vfs_filesystem *filesystem,
                             struct vfs_node *directory,
                             const char *name,
                             size_t name_length,
                             struct vfs_node **node_out);
    enum vfs_result (*unlink)(struct vfs_filesystem *filesystem,
                              struct vfs_node *directory,
                              const char *name,
                              size_t name_length);
    enum vfs_result (*rmdir)(struct vfs_filesystem *filesystem,
                             struct vfs_node *directory,
                             const char *name,
                             size_t name_length);
    enum vfs_result (*rename)(struct vfs_filesystem *filesystem,
                              struct vfs_node *old_directory,
                              const char *old_name,
                              size_t old_name_length,
                              struct vfs_node *new_directory,
                              const char *new_name,
                              size_t new_name_length);
    enum vfs_result (*read)(struct vfs_filesystem *filesystem,
                            struct vfs_node *node,
                            uint64_t offset,
                            void *buffer,
                            size_t length,
                            size_t *transferred_out);
    enum vfs_result (*write)(struct vfs_filesystem *filesystem,
                             struct vfs_node *node,
                             uint64_t offset,
                             const void *buffer,
                             size_t length,
                             size_t *transferred_out);
    enum vfs_result (*truncate)(struct vfs_filesystem *filesystem,
                                struct vfs_node *node,
                                uint64_t size);
    enum vfs_result (*readdir)(struct vfs_filesystem *filesystem,
                               struct vfs_node *directory,
                               uint64_t index,
                               struct vfs_dirent *entry_out);
};

struct vfs_filesystem {
    uint64_t id;
    const struct vfs_operations *operations;
    struct vfs_node *root;
    void *backend_context;
    bool valid;
};

struct vfs_node {
    uint64_t id;
    enum vfs_node_type type;
    struct vfs_filesystem *filesystem;
    struct vfs_node *parent;
    void *backend_context;
    uint32_t reference_count;
    bool valid;
};

struct vfs_path {
    struct vfs_mount *mount;
    struct vfs_node *node;
};

struct vfs_handle {
    struct vfs_path path;
    uint64_t offset;
    uint32_t access;
    bool open;
};

struct vfs_stats {
    uint64_t mount_count;
    uint64_t path_reference_count;
    uint64_t open_handle_count;
};

bool vfs_filesystem_prepare(struct vfs_filesystem *filesystem,
                            uint64_t id,
                            const struct vfs_operations *operations,
                            void *backend_context);
bool vfs_node_prepare(struct vfs_node *node,
                      struct vfs_filesystem *filesystem,
                      uint64_t id,
                      enum vfs_node_type type,
                      struct vfs_node *parent,
                      void *backend_context);
bool vfs_filesystem_set_root(struct vfs_filesystem *filesystem,
                             struct vfs_node *root);

enum vfs_result vfs_init(struct vfs_filesystem *root_filesystem);
enum vfs_result vfs_shutdown(void);
bool vfs_is_initialized(void);
bool vfs_get_stats(struct vfs_stats *stats_out);

enum vfs_result vfs_get_root(struct vfs_path *path_out);
enum vfs_result vfs_path_clone(const struct vfs_path *source,
                               struct vfs_path *destination);
enum vfs_result vfs_path_release(struct vfs_path *path);
bool vfs_path_equal(const struct vfs_path *left,
                    const struct vfs_path *right);
bool vfs_path_is_directory(const struct vfs_path *path);

enum vfs_result vfs_resolve(const struct vfs_path *start,
                            const char *path,
                            struct vfs_path *path_out);
enum vfs_result vfs_resolve_process(const struct process *process,
                                    const char *path,
                                    struct vfs_path *path_out);

enum vfs_result vfs_mount_filesystem(struct vfs_filesystem *filesystem,
                                     const struct vfs_path *target);

enum vfs_result vfs_create_at(const struct vfs_path *directory,
                              const char *name,
                              struct vfs_path *path_out);
enum vfs_result vfs_mkdir_at(const struct vfs_path *directory,
                             const char *name,
                             struct vfs_path *path_out);
enum vfs_result vfs_unlink_at(const struct vfs_path *directory,
                              const char *name);
enum vfs_result vfs_rmdir_at(const struct vfs_path *directory,
                             const char *name);
enum vfs_result vfs_rename_at(const struct vfs_path *old_directory,
                              const char *old_name,
                              const struct vfs_path *new_directory,
                              const char *new_name);
enum vfs_result vfs_truncate_path(const struct vfs_path *path,
                                  uint64_t size);
enum vfs_result vfs_readdir_path(const struct vfs_path *directory,
                                 uint64_t index,
                                 struct vfs_dirent *entry_out);

enum vfs_result vfs_handle_open(const struct vfs_path *path,
                                uint32_t access,
                                struct vfs_handle *handle_out);
enum vfs_result vfs_handle_read(struct vfs_handle *handle,
                                void *buffer,
                                size_t length,
                                size_t *transferred_out);
enum vfs_result vfs_handle_write(struct vfs_handle *handle,
                                 const void *buffer,
                                 size_t length,
                                 size_t *transferred_out);
enum vfs_result vfs_handle_get_offset(const struct vfs_handle *handle,
                                      uint64_t *offset_out);
enum vfs_result vfs_handle_close(struct vfs_handle *handle);

#endif
