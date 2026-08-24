#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/process.h>
#include <boring/vfs.h>

struct vfs_mount {
    uint64_t id;
    struct vfs_filesystem *filesystem;
    struct vfs_mount *parent;
    struct vfs_node *mountpoint;
    bool active;
};

static struct vfs_mount vfs_mounts[VFS_MOUNT_MAX];
static bool vfs_initialized;
static uint64_t vfs_next_mount_id;
static uint64_t vfs_path_reference_count;
static uint64_t vfs_open_handle_count;

static bool vfs_node_type_valid(enum vfs_node_type type) {
    return (type == VFS_NODE_DIRECTORY) || (type == VFS_NODE_REGULAR);
}

static bool vfs_node_valid_for_filesystem(const struct vfs_node *node,
                                          const struct vfs_filesystem *filesystem) {
    return (node != NULL) && node->valid && (node->id != 0ULL) &&
           vfs_node_type_valid(node->type) &&
           (node->filesystem == filesystem);
}

static bool vfs_filesystem_valid(const struct vfs_filesystem *filesystem) {
    return (filesystem != NULL) && filesystem->valid &&
           (filesystem->id != 0ULL) && (filesystem->operations != NULL) &&
           vfs_node_valid_for_filesystem(filesystem->root, filesystem) &&
           (filesystem->root->type == VFS_NODE_DIRECTORY) &&
           (filesystem->root->parent == filesystem->root);
}

static void vfs_mount_clear(struct vfs_mount *mount) {
    if (mount == NULL) {
        return;
    }
    mount->id = 0ULL;
    mount->filesystem = NULL;
    mount->parent = NULL;
    mount->mountpoint = NULL;
    mount->active = false;
}

static bool vfs_mount_valid(const struct vfs_mount *mount) {
    size_t index;

    if ((!vfs_initialized) || (mount == NULL)) {
        return false;
    }

    for (index = 0U; index < (size_t)VFS_MOUNT_MAX; ++index) {
        if (mount == &vfs_mounts[index]) {
            return mount->active && (mount->id != 0ULL) &&
                   vfs_filesystem_valid(mount->filesystem);
        }
    }
    return false;
}

static bool vfs_path_valid(const struct vfs_path *path) {
    return (path != NULL) && vfs_mount_valid(path->mount) &&
           vfs_node_valid_for_filesystem(path->node,
                                         path->mount->filesystem);
}

static enum vfs_result vfs_node_retain(struct vfs_node *node) {
    if ((node == NULL) || !node->valid) {
        return VFS_RESULT_CORRUPT;
    }
    if (node->reference_count == UINT32_MAX) {
        return VFS_RESULT_OVERFLOW;
    }
    ++node->reference_count;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_node_release(struct vfs_node *node) {
    if ((node == NULL) || !node->valid || (node->reference_count == 0U)) {
        return VFS_RESULT_CORRUPT;
    }
    --node->reference_count;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_path_acquire(struct vfs_mount *mount,
                                        struct vfs_node *node,
                                        struct vfs_path *path_out) {
    enum vfs_result result;

    if ((path_out == NULL) || (path_out->mount != NULL) ||
        (path_out->node != NULL) || !vfs_mount_valid(mount) ||
        !vfs_node_valid_for_filesystem(node, mount->filesystem)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (vfs_path_reference_count == UINT64_MAX) {
        return VFS_RESULT_OVERFLOW;
    }

    result = vfs_node_retain(node);
    if (result != VFS_RESULT_OK) {
        return result;
    }

    path_out->mount = mount;
    path_out->node = node;
    ++vfs_path_reference_count;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_path_replace(struct vfs_path *path,
                                        struct vfs_mount *mount,
                                        struct vfs_node *node) {
    struct vfs_path replacement = { NULL, NULL };
    enum vfs_result result;

    if (!vfs_path_valid(path)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }

    result = vfs_path_acquire(mount, node, &replacement);
    if (result != VFS_RESULT_OK) {
        return result;
    }

    result = vfs_path_release(path);
    if (result != VFS_RESULT_OK) {
        (void)vfs_path_release(&replacement);
        return result;
    }

    *path = replacement;
    return VFS_RESULT_OK;
}

static bool vfs_bounded_length(const char *text,
                               size_t maximum,
                               size_t *length_out) {
    size_t length;

    if ((text == NULL) || (length_out == NULL)) {
        return false;
    }

    for (length = 0U; length <= maximum; ++length) {
        if (text[length] == '\0') {
            *length_out = length;
            return true;
        }
    }
    return false;
}

static enum vfs_result vfs_validate_name(const char *name,
                                         size_t *length_out) {
    size_t length;
    size_t index;

    if ((name == NULL) || (length_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (!vfs_bounded_length(name, (size_t)VFS_NAME_MAX, &length)) {
        return VFS_RESULT_NAME_TOO_LONG;
    }
    if (length == 0U) {
        return VFS_RESULT_EMPTY_PATH;
    }
    if (((length == 1U) && (name[0] == '.')) ||
        ((length == 2U) && (name[0] == '.') && (name[1] == '.'))) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    for (index = 0U; index < length; ++index) {
        if (name[index] == '/') {
            return VFS_RESULT_INVALID_ARGUMENT;
        }
    }

    *length_out = length;
    return VFS_RESULT_OK;
}

static struct vfs_mount *vfs_child_mount(struct vfs_mount *parent,
                                         struct vfs_node *mountpoint) {
    size_t index;

    for (index = 1U; index < (size_t)VFS_MOUNT_MAX; ++index) {
        struct vfs_mount *const candidate = &vfs_mounts[index];
        if (candidate->active && (candidate->parent == parent) &&
            (candidate->mountpoint == mountpoint)) {
            return candidate;
        }
    }
    return NULL;
}

static bool vfs_filesystem_is_mounted(const struct vfs_filesystem *filesystem) {
    size_t index;

    for (index = 0U; index < (size_t)VFS_MOUNT_MAX; ++index) {
        if (vfs_mounts[index].active &&
            (vfs_mounts[index].filesystem == filesystem)) {
            return true;
        }
    }
    return false;
}

static enum vfs_result vfs_follow_child_mount(struct vfs_path *path) {
    struct vfs_mount *child;

    if (!vfs_path_valid(path)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }

    child = vfs_child_mount(path->mount, path->node);
    if (child == NULL) {
        return VFS_RESULT_OK;
    }

    return vfs_path_replace(path, child, child->filesystem->root);
}

static enum vfs_result vfs_step_parent(struct vfs_path *path) {
    struct vfs_node *parent_node;

    if (!vfs_path_valid(path)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }

    if (path->node == path->mount->filesystem->root) {
        if (path->mount == &vfs_mounts[0]) {
            return VFS_RESULT_OK;
        }
        if ((path->mount->parent == NULL) ||
            (path->mount->mountpoint == NULL) ||
            !vfs_mount_valid(path->mount->parent)) {
            return VFS_RESULT_CORRUPT;
        }
        parent_node = path->mount->mountpoint->parent;
        if (!vfs_node_valid_for_filesystem(
                parent_node, path->mount->parent->filesystem) ||
            (parent_node->type != VFS_NODE_DIRECTORY)) {
            return VFS_RESULT_CORRUPT;
        }
        return vfs_path_replace(path, path->mount->parent, parent_node);
    }

    parent_node = path->node->parent;
    if (!vfs_node_valid_for_filesystem(parent_node,
                                       path->mount->filesystem) ||
        (parent_node->type != VFS_NODE_DIRECTORY)) {
        return VFS_RESULT_CORRUPT;
    }
    return vfs_path_replace(path, path->mount, parent_node);
}

static enum vfs_result vfs_validate_directory(const struct vfs_path *path) {
    if (!vfs_path_valid(path)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (path->node->type != VFS_NODE_DIRECTORY) {
        return VFS_RESULT_NOT_DIRECTORY;
    }
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_validate_regular(const struct vfs_path *path) {
    if (!vfs_path_valid(path)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (path->node->type != VFS_NODE_REGULAR) {
        return VFS_RESULT_NOT_REGULAR;
    }
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_validate_backend_child(
    struct vfs_filesystem *filesystem,
    struct vfs_node *directory,
    struct vfs_node *node,
    enum vfs_node_type expected_type,
    bool require_type) {
    if (!vfs_node_valid_for_filesystem(node, filesystem) ||
        (node == directory) || (node->parent != directory)) {
        return VFS_RESULT_CORRUPT;
    }
    if (require_type && (node->type != expected_type)) {
        return VFS_RESULT_CORRUPT;
    }
    return VFS_RESULT_OK;
}

static bool vfs_handle_valid(const struct vfs_handle *handle) {
    return (handle != NULL) && handle->open &&
           ((handle->access & ~VFS_ACCESS_MASK) == 0U) &&
           ((handle->access & VFS_ACCESS_MASK) != 0U) &&
           vfs_path_valid(&handle->path);
}

bool vfs_filesystem_prepare(struct vfs_filesystem *filesystem,
                            uint64_t id,
                            const struct vfs_operations *operations,
                            void *backend_context) {
    if ((filesystem == NULL) || (id == 0ULL) || (operations == NULL)) {
        return false;
    }

    filesystem->id = id;
    filesystem->operations = operations;
    filesystem->root = NULL;
    filesystem->backend_context = backend_context;
    filesystem->valid = true;
    return true;
}

bool vfs_node_prepare(struct vfs_node *node,
                      struct vfs_filesystem *filesystem,
                      uint64_t id,
                      enum vfs_node_type type,
                      struct vfs_node *parent,
                      void *backend_context) {
    if ((node == NULL) || (filesystem == NULL) || !filesystem->valid ||
        (id == 0ULL) || !vfs_node_type_valid(type)) {
        return false;
    }
    if (parent != NULL) {
        if (!vfs_node_valid_for_filesystem(parent, filesystem) ||
            (parent->type != VFS_NODE_DIRECTORY) || (parent == node)) {
            return false;
        }
    } else if (type != VFS_NODE_DIRECTORY) {
        return false;
    }

    node->id = id;
    node->type = type;
    node->filesystem = filesystem;
    node->parent = parent;
    node->backend_context = backend_context;
    node->reference_count = 0U;
    node->valid = true;
    return true;
}

bool vfs_filesystem_set_root(struct vfs_filesystem *filesystem,
                             struct vfs_node *root) {
    if ((filesystem == NULL) || !filesystem->valid ||
        (filesystem->root != NULL) ||
        !vfs_node_valid_for_filesystem(root, filesystem) ||
        (root->type != VFS_NODE_DIRECTORY) || (root->parent != NULL)) {
        return false;
    }

    root->parent = root;
    filesystem->root = root;
    return true;
}

enum vfs_result vfs_init(struct vfs_filesystem *root_filesystem) {
    size_t index;
    enum vfs_result result;

    if (vfs_initialized) {
        return VFS_RESULT_ALREADY_INITIALIZED;
    }
    if (!vfs_filesystem_valid(root_filesystem)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }

    for (index = 0U; index < (size_t)VFS_MOUNT_MAX; ++index) {
        vfs_mount_clear(&vfs_mounts[index]);
    }
    vfs_next_mount_id = 1ULL;
    vfs_path_reference_count = 0ULL;
    vfs_open_handle_count = 0ULL;

    vfs_mounts[0].id = vfs_next_mount_id;
    vfs_mounts[0].filesystem = root_filesystem;
    vfs_mounts[0].parent = NULL;
    vfs_mounts[0].mountpoint = NULL;
    vfs_mounts[0].active = true;
    ++vfs_next_mount_id;
    vfs_initialized = true;

    result = vfs_node_retain(root_filesystem->root);
    if (result != VFS_RESULT_OK) {
        vfs_mount_clear(&vfs_mounts[0]);
        vfs_initialized = false;
        return result;
    }
    return VFS_RESULT_OK;
}

enum vfs_result vfs_shutdown(void) {
    size_t index;

    if (!vfs_initialized) {
        return VFS_RESULT_NOT_INITIALIZED;
    }
    if ((vfs_path_reference_count != 0ULL) ||
        (vfs_open_handle_count != 0ULL)) {
        return VFS_RESULT_BUSY;
    }

    for (index = (size_t)VFS_MOUNT_MAX; index > 0U; --index) {
        struct vfs_mount *const mount = &vfs_mounts[index - 1U];
        if (!mount->active) {
            continue;
        }
        if ((vfs_node_release(mount->filesystem->root) != VFS_RESULT_OK) ||
            ((mount->mountpoint != NULL) &&
             (vfs_node_release(mount->mountpoint) != VFS_RESULT_OK))) {
            return VFS_RESULT_CORRUPT;
        }
        vfs_mount_clear(mount);
    }

    vfs_initialized = false;
    vfs_next_mount_id = 0ULL;
    return VFS_RESULT_OK;
}

bool vfs_is_initialized(void) {
    return vfs_initialized;
}

bool vfs_get_stats(struct vfs_stats *stats_out) {
    size_t index;
    uint64_t mount_count = 0ULL;

    if ((!vfs_initialized) || (stats_out == NULL)) {
        return false;
    }
    for (index = 0U; index < (size_t)VFS_MOUNT_MAX; ++index) {
        if (vfs_mounts[index].active) {
            ++mount_count;
        }
    }
    stats_out->mount_count = mount_count;
    stats_out->path_reference_count = vfs_path_reference_count;
    stats_out->open_handle_count = vfs_open_handle_count;
    return true;
}

enum vfs_result vfs_get_root(struct vfs_path *path_out) {
    if (!vfs_initialized) {
        return VFS_RESULT_NOT_INITIALIZED;
    }
    return vfs_path_acquire(&vfs_mounts[0],
                            vfs_mounts[0].filesystem->root,
                            path_out);
}

enum vfs_result vfs_path_clone(const struct vfs_path *source,
                               struct vfs_path *destination) {
    if (!vfs_path_valid(source)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    return vfs_path_acquire(source->mount, source->node, destination);
}

enum vfs_result vfs_path_release(struct vfs_path *path) {
    enum vfs_result result;

    if (!vfs_path_valid(path) || (vfs_path_reference_count == 0ULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }

    result = vfs_node_release(path->node);
    if (result != VFS_RESULT_OK) {
        return result;
    }

    --vfs_path_reference_count;
    path->mount = NULL;
    path->node = NULL;
    return VFS_RESULT_OK;
}

bool vfs_path_equal(const struct vfs_path *left,
                    const struct vfs_path *right) {
    return vfs_path_valid(left) && vfs_path_valid(right) &&
           (left->mount == right->mount) && (left->node == right->node);
}

bool vfs_path_is_directory(const struct vfs_path *path) {
    return vfs_path_valid(path) && (path->node->type == VFS_NODE_DIRECTORY);
}

enum vfs_result vfs_resolve(const struct vfs_path *start,
                            const char *path,
                            struct vfs_path *path_out) {
    struct vfs_path current = { NULL, NULL };
    size_t path_length;
    size_t index;
    enum vfs_result result;

    if ((!vfs_initialized) || (path == NULL) || (path_out == NULL) ||
        (path_out->mount != NULL) || (path_out->node != NULL)) {
        return vfs_initialized ? VFS_RESULT_INVALID_ARGUMENT
                               : VFS_RESULT_NOT_INITIALIZED;
    }
    if (!vfs_bounded_length(path, (size_t)VFS_PATH_MAX, &path_length)) {
        return VFS_RESULT_PATH_TOO_LONG;
    }
    if (path_length == 0U) {
        return VFS_RESULT_EMPTY_PATH;
    }

    if (path[0] == '/') {
        result = vfs_get_root(&current);
    } else if (vfs_path_valid(start)) {
        result = vfs_path_clone(start, &current);
    } else {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (result != VFS_RESULT_OK) {
        return result;
    }

    index = 0U;
    while (index < path_length) {
        size_t component_start;
        size_t component_length;

        while ((index < path_length) && (path[index] == '/')) {
            ++index;
        }
        if (index == path_length) {
            break;
        }

        component_start = index;
        while ((index < path_length) && (path[index] != '/')) {
            ++index;
        }
        component_length = index - component_start;
        if (component_length > (size_t)VFS_NAME_MAX) {
            (void)vfs_path_release(&current);
            return VFS_RESULT_NAME_TOO_LONG;
        }

        if ((component_length == 1U) &&
            (path[component_start] == '.')) {
            continue;
        }
        if ((component_length == 2U) &&
            (path[component_start] == '.') &&
            (path[component_start + 1U] == '.')) {
            result = vfs_step_parent(&current);
            if (result != VFS_RESULT_OK) {
                (void)vfs_path_release(&current);
                return result;
            }
            continue;
        }

        if (current.node->type != VFS_NODE_DIRECTORY) {
            (void)vfs_path_release(&current);
            return VFS_RESULT_NOT_DIRECTORY;
        }
        if (current.mount->filesystem->operations->lookup == NULL) {
            (void)vfs_path_release(&current);
            return VFS_RESULT_NOT_SUPPORTED;
        }

        {
            struct vfs_node *node = NULL;
            struct vfs_path next = { NULL, NULL };

            result = current.mount->filesystem->operations->lookup(
                current.mount->filesystem,
                current.node,
                &path[component_start],
                component_length,
                &node);
            if (result != VFS_RESULT_OK) {
                (void)vfs_path_release(&current);
                return result;
            }
            result = vfs_validate_backend_child(current.mount->filesystem,
                                                current.node,
                                                node,
                                                VFS_NODE_REGULAR,
                                                false);
            if (result != VFS_RESULT_OK) {
                (void)vfs_path_release(&current);
                return result;
            }
            result = vfs_path_acquire(current.mount, node, &next);
            if (result != VFS_RESULT_OK) {
                (void)vfs_path_release(&current);
                return result;
            }
            result = vfs_path_release(&current);
            if (result != VFS_RESULT_OK) {
                (void)vfs_path_release(&next);
                return result;
            }
            current = next;
        }

        result = vfs_follow_child_mount(&current);
        if (result != VFS_RESULT_OK) {
            (void)vfs_path_release(&current);
            return result;
        }
    }

    *path_out = current;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_resolve_process(const struct process *process,
                                    const char *path,
                                    struct vfs_path *path_out) {
    if ((process == NULL) || !process_is_alive(process) || (path == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (path[0] == '/') {
        return vfs_resolve(NULL, path, path_out);
    }
    if (!process->cwd_valid) {
        return VFS_RESULT_NO_CWD;
    }
    return vfs_resolve(&process->cwd, path, path_out);
}

enum vfs_result vfs_mount_filesystem(struct vfs_filesystem *filesystem,
                                     const struct vfs_path *target) {
    size_t index;
    struct vfs_mount *slot = NULL;
    enum vfs_result result;

    if (!vfs_initialized) {
        return VFS_RESULT_NOT_INITIALIZED;
    }
    result = vfs_validate_directory(target);
    if ((result != VFS_RESULT_OK) || !vfs_filesystem_valid(filesystem)) {
        return (result != VFS_RESULT_OK) ? result
                                         : VFS_RESULT_INVALID_ARGUMENT;
    }
    if ((target->node == target->mount->filesystem->root) ||
        (filesystem == target->mount->filesystem)) {
        return VFS_RESULT_MOUNT_CONFLICT;
    }
    if (vfs_filesystem_is_mounted(filesystem)) {
        return VFS_RESULT_ALREADY_MOUNTED;
    }
    if (vfs_child_mount(target->mount, target->node) != NULL) {
        return VFS_RESULT_MOUNT_CONFLICT;
    }
    if (vfs_next_mount_id == UINT64_MAX) {
        return VFS_RESULT_OVERFLOW;
    }

    for (index = 1U; index < (size_t)VFS_MOUNT_MAX; ++index) {
        if (!vfs_mounts[index].active) {
            slot = &vfs_mounts[index];
            break;
        }
    }
    if (slot == NULL) {
        return VFS_RESULT_NO_SPACE;
    }

    result = vfs_node_retain(target->node);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = vfs_node_retain(filesystem->root);
    if (result != VFS_RESULT_OK) {
        (void)vfs_node_release(target->node);
        return result;
    }

    slot->id = vfs_next_mount_id;
    slot->filesystem = filesystem;
    slot->parent = target->mount;
    slot->mountpoint = target->node;
    slot->active = true;
    ++vfs_next_mount_id;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_create_common(const struct vfs_path *directory,
                                         const char *name,
                                         struct vfs_path *path_out,
                                         bool directory_requested) {
    size_t name_length;
    struct vfs_node *node = NULL;
    enum vfs_result result;

    result = vfs_validate_directory(directory);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if ((path_out == NULL) || (path_out->mount != NULL) ||
        (path_out->node != NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    result = vfs_validate_name(name, &name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }

    if (directory_requested) {
        if (directory->mount->filesystem->operations->mkdir == NULL) {
            return VFS_RESULT_NOT_SUPPORTED;
        }
        result = directory->mount->filesystem->operations->mkdir(
            directory->mount->filesystem,
            directory->node,
            name,
            name_length,
            &node);
    } else {
        if (directory->mount->filesystem->operations->create == NULL) {
            return VFS_RESULT_NOT_SUPPORTED;
        }
        result = directory->mount->filesystem->operations->create(
            directory->mount->filesystem,
            directory->node,
            name,
            name_length,
            &node);
    }
    if (result != VFS_RESULT_OK) {
        return result;
    }

    result = vfs_validate_backend_child(
        directory->mount->filesystem,
        directory->node,
        node,
        directory_requested ? VFS_NODE_DIRECTORY : VFS_NODE_REGULAR,
        true);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    return vfs_path_acquire(directory->mount, node, path_out);
}

enum vfs_result vfs_create_at(const struct vfs_path *directory,
                              const char *name,
                              struct vfs_path *path_out) {
    return vfs_create_common(directory, name, path_out, false);
}

enum vfs_result vfs_mkdir_at(const struct vfs_path *directory,
                             const char *name,
                             struct vfs_path *path_out) {
    return vfs_create_common(directory, name, path_out, true);
}

static enum vfs_result vfs_remove_common(const struct vfs_path *directory,
                                         const char *name,
                                         bool directory_requested) {
    size_t name_length;
    enum vfs_result result;

    result = vfs_validate_directory(directory);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = vfs_validate_name(name, &name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }

    if (directory_requested) {
        if (directory->mount->filesystem->operations->rmdir == NULL) {
            return VFS_RESULT_NOT_SUPPORTED;
        }
        return directory->mount->filesystem->operations->rmdir(
            directory->mount->filesystem,
            directory->node,
            name,
            name_length);
    }

    if (directory->mount->filesystem->operations->unlink == NULL) {
        return VFS_RESULT_NOT_SUPPORTED;
    }
    return directory->mount->filesystem->operations->unlink(
        directory->mount->filesystem,
        directory->node,
        name,
        name_length);
}

enum vfs_result vfs_unlink_at(const struct vfs_path *directory,
                              const char *name) {
    return vfs_remove_common(directory, name, false);
}

enum vfs_result vfs_rmdir_at(const struct vfs_path *directory,
                             const char *name) {
    return vfs_remove_common(directory, name, true);
}

enum vfs_result vfs_rename_at(const struct vfs_path *old_directory,
                              const char *old_name,
                              const struct vfs_path *new_directory,
                              const char *new_name) {
    size_t old_name_length;
    size_t new_name_length;
    enum vfs_result result;

    result = vfs_validate_directory(old_directory);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = vfs_validate_directory(new_directory);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = vfs_validate_name(old_name, &old_name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = vfs_validate_name(new_name, &new_name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if ((old_directory->mount != new_directory->mount) ||
        (old_directory->mount->filesystem !=
         new_directory->mount->filesystem)) {
        return VFS_RESULT_CROSS_FILESYSTEM;
    }
    if (old_directory->mount->filesystem->operations->rename == NULL) {
        return VFS_RESULT_NOT_SUPPORTED;
    }

    return old_directory->mount->filesystem->operations->rename(
        old_directory->mount->filesystem,
        old_directory->node,
        old_name,
        old_name_length,
        new_directory->node,
        new_name,
        new_name_length);
}

enum vfs_result vfs_truncate_path(const struct vfs_path *path,
                                  uint64_t size) {
    enum vfs_result result = vfs_validate_regular(path);

    if (result != VFS_RESULT_OK) {
        return result;
    }
    if (path->mount->filesystem->operations->truncate == NULL) {
        return VFS_RESULT_NOT_SUPPORTED;
    }
    return path->mount->filesystem->operations->truncate(
        path->mount->filesystem, path->node, size);
}

enum vfs_result vfs_readdir_path(const struct vfs_path *directory,
                                 uint64_t index,
                                 struct vfs_dirent *entry_out) {
    enum vfs_result result;
    size_t entry_index;

    result = vfs_validate_directory(directory);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if (entry_out == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (directory->mount->filesystem->operations->readdir == NULL) {
        return VFS_RESULT_NOT_SUPPORTED;
    }

    entry_out->node_id = 0ULL;
    entry_out->type = (enum vfs_node_type)0;
    entry_out->name_length = 0U;
    entry_out->name[0] = '\0';
    result = directory->mount->filesystem->operations->readdir(
        directory->mount->filesystem,
        directory->node,
        index,
        entry_out);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if ((entry_out->node_id == 0ULL) ||
        !vfs_node_type_valid(entry_out->type) ||
        (entry_out->name_length == 0U) ||
        (entry_out->name_length > (size_t)VFS_NAME_MAX) ||
        (entry_out->name[entry_out->name_length] != '\0')) {
        return VFS_RESULT_CORRUPT;
    }
    if (((entry_out->name_length == 1U) &&
         (entry_out->name[0] == '.')) ||
        ((entry_out->name_length == 2U) &&
         (entry_out->name[0] == '.') && (entry_out->name[1] == '.'))) {
        return VFS_RESULT_CORRUPT;
    }
    for (entry_index = 0U; entry_index < entry_out->name_length;
         ++entry_index) {
        if (entry_out->name[entry_index] == '/') {
            return VFS_RESULT_CORRUPT;
        }
    }
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_open(const struct vfs_path *path,
                                uint32_t access,
                                struct vfs_handle *handle_out) {
    enum vfs_result result;

    result = vfs_validate_regular(path);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if ((handle_out == NULL) || handle_out->open ||
        (handle_out->path.mount != NULL) || (handle_out->path.node != NULL) ||
        ((access & VFS_ACCESS_MASK) == 0U) ||
        ((access & ~VFS_ACCESS_MASK) != 0U)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (vfs_open_handle_count == UINT64_MAX) {
        return VFS_RESULT_OVERFLOW;
    }

    result = vfs_path_clone(path, &handle_out->path);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    handle_out->offset = 0ULL;
    handle_out->access = access;
    handle_out->open = true;
    ++vfs_open_handle_count;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_read(struct vfs_handle *handle,
                                void *buffer,
                                size_t length,
                                size_t *transferred_out) {
    size_t transferred = 0U;
    enum vfs_result result;

    if (!vfs_handle_valid(handle) || (buffer == NULL) ||
        (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *transferred_out = 0U;
    if ((handle->access & VFS_ACCESS_READ) == 0U) {
        return VFS_RESULT_ACCESS_DENIED;
    }
    if (length > (size_t)VFS_IO_MAX) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if ((uint64_t)length > (UINT64_MAX - handle->offset)) {
        return VFS_RESULT_OVERFLOW;
    }
    if (handle->path.mount->filesystem->operations->read == NULL) {
        return VFS_RESULT_NOT_SUPPORTED;
    }

    result = handle->path.mount->filesystem->operations->read(
        handle->path.mount->filesystem,
        handle->path.node,
        handle->offset,
        buffer,
        length,
        &transferred);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if (transferred > length) {
        return VFS_RESULT_CORRUPT;
    }
    handle->offset += (uint64_t)transferred;
    *transferred_out = transferred;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_write(struct vfs_handle *handle,
                                 const void *buffer,
                                 size_t length,
                                 size_t *transferred_out) {
    size_t transferred = 0U;
    enum vfs_result result;

    if (!vfs_handle_valid(handle) || (buffer == NULL) ||
        (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *transferred_out = 0U;
    if ((handle->access & VFS_ACCESS_WRITE) == 0U) {
        return VFS_RESULT_ACCESS_DENIED;
    }
    if (length > (size_t)VFS_IO_MAX) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if ((uint64_t)length > (UINT64_MAX - handle->offset)) {
        return VFS_RESULT_OVERFLOW;
    }
    if (handle->path.mount->filesystem->operations->write == NULL) {
        return VFS_RESULT_NOT_SUPPORTED;
    }

    result = handle->path.mount->filesystem->operations->write(
        handle->path.mount->filesystem,
        handle->path.node,
        handle->offset,
        buffer,
        length,
        &transferred);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if (transferred > length) {
        return VFS_RESULT_CORRUPT;
    }
    handle->offset += (uint64_t)transferred;
    *transferred_out = transferred;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_get_offset(const struct vfs_handle *handle,
                                      uint64_t *offset_out) {
    if (!vfs_handle_valid(handle) || (offset_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *offset_out = handle->offset;
    return VFS_RESULT_OK;
}

enum vfs_result vfs_handle_close(struct vfs_handle *handle) {
    enum vfs_result result;

    if (!vfs_handle_valid(handle) || (vfs_open_handle_count == 0ULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }

    result = vfs_path_release(&handle->path);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    handle->offset = 0ULL;
    handle->access = 0U;
    handle->open = false;
    --vfs_open_handle_count;
    return VFS_RESULT_OK;
}
