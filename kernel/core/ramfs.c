#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/heap.h>
#include <boring/ramfs.h>

struct ramfs_node {
    struct vfs_node vfs;
    struct ramfs *owner;
    char name[VFS_NAME_MAX + 1U];
    size_t name_length;
    uint8_t *data;
    uint64_t size;
    size_t capacity;
    bool occupied;
};

struct ramfs {
    struct vfs_filesystem filesystem;
    struct ramfs_node nodes[RAMFS_MAX_NODES];
    uint64_t next_node_id;
    bool registered;
};

static struct ramfs *ramfs_instances[RAMFS_MAX_FILESYSTEMS];

static enum vfs_result ramfs_lookup(struct vfs_filesystem *filesystem,
                                    struct vfs_node *directory,
                                    const char *name,
                                    size_t name_length,
                                    struct vfs_node **node_out);
static enum vfs_result ramfs_create(struct vfs_filesystem *filesystem,
                                    struct vfs_node *directory,
                                    const char *name,
                                    size_t name_length,
                                    struct vfs_node **node_out);
static enum vfs_result ramfs_mkdir(struct vfs_filesystem *filesystem,
                                   struct vfs_node *directory,
                                   const char *name,
                                   size_t name_length,
                                   struct vfs_node **node_out);
static enum vfs_result ramfs_unlink(struct vfs_filesystem *filesystem,
                                    struct vfs_node *directory,
                                    const char *name,
                                    size_t name_length);
static enum vfs_result ramfs_rmdir(struct vfs_filesystem *filesystem,
                                   struct vfs_node *directory,
                                   const char *name,
                                   size_t name_length);
static enum vfs_result ramfs_rename(struct vfs_filesystem *filesystem,
                                    struct vfs_node *old_directory,
                                    const char *old_name,
                                    size_t old_name_length,
                                    struct vfs_node *new_directory,
                                    const char *new_name,
                                    size_t new_name_length);
static enum vfs_result ramfs_read(struct vfs_filesystem *filesystem,
                                  struct vfs_node *node,
                                  uint64_t offset,
                                  void *buffer,
                                  size_t length,
                                  size_t *transferred_out);
static enum vfs_result ramfs_write(struct vfs_filesystem *filesystem,
                                   struct vfs_node *node,
                                   uint64_t offset,
                                   const void *buffer,
                                   size_t length,
                                   size_t *transferred_out);
static enum vfs_result ramfs_truncate(struct vfs_filesystem *filesystem,
                                      struct vfs_node *node,
                                      uint64_t size);
static enum vfs_result ramfs_readdir(struct vfs_filesystem *filesystem,
                                     struct vfs_node *directory,
                                     uint64_t index,
                                     struct vfs_dirent *entry_out);

static const struct vfs_operations ramfs_operations = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
    .unlink = ramfs_unlink,
    .rmdir = ramfs_rmdir,
    .rename = ramfs_rename,
    .read = ramfs_read,
    .write = ramfs_write,
    .truncate = ramfs_truncate,
    .readdir = ramfs_readdir
};

static void ramfs_zero_bytes(uint8_t *destination, size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = 0U;
    }
}

static void ramfs_copy_bytes(uint8_t *destination,
                             const uint8_t *source,
                             size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static void ramfs_node_clear(struct ramfs_node *node) {
    size_t index;

    if (node == NULL) {
        return;
    }
    node->vfs.id = 0ULL;
    node->vfs.type = (enum vfs_node_type)0;
    node->vfs.filesystem = NULL;
    node->vfs.parent = NULL;
    node->vfs.backend_context = NULL;
    node->vfs.reference_count = 0U;
    node->vfs.valid = false;
    node->owner = NULL;
    for (index = 0U; index <= (size_t)VFS_NAME_MAX; ++index) {
        node->name[index] = '\0';
    }
    node->name_length = 0U;
    node->data = NULL;
    node->size = 0ULL;
    node->capacity = 0U;
    node->occupied = false;
}

static struct ramfs *ramfs_instance_from_pointer(const struct ramfs *candidate) {
    size_t index;

    if (candidate == NULL) {
        return NULL;
    }
    for (index = 0U; index < (size_t)RAMFS_MAX_FILESYSTEMS; ++index) {
        if (ramfs_instances[index] == candidate) {
            return ramfs_instances[index];
        }
    }
    return NULL;
}

static struct ramfs *ramfs_instance_from_filesystem(
    const struct vfs_filesystem *filesystem) {
    size_t index;

    if (filesystem == NULL) {
        return NULL;
    }
    for (index = 0U; index < (size_t)RAMFS_MAX_FILESYSTEMS; ++index) {
        struct ramfs *const candidate = ramfs_instances[index];
        if ((candidate != NULL) && (&candidate->filesystem == filesystem)) {
            if (!candidate->registered || !candidate->filesystem.valid ||
                (candidate->filesystem.operations != &ramfs_operations) ||
                (candidate->filesystem.backend_context != candidate)) {
                return NULL;
            }
            return candidate;
        }
    }
    return NULL;
}

static struct ramfs_node *ramfs_node_from_vfs(struct ramfs *ramfs,
                                              const struct vfs_node *node) {
    size_t index;

    if ((ramfs == NULL) || (node == NULL)) {
        return NULL;
    }
    for (index = 0U; index < (size_t)RAMFS_MAX_NODES; ++index) {
        struct ramfs_node *const candidate = &ramfs->nodes[index];
        if (&candidate->vfs == node) {
            if (!candidate->occupied || (candidate->owner != ramfs) ||
                !candidate->vfs.valid || (candidate->vfs.id == 0ULL) ||
                (candidate->vfs.filesystem != &ramfs->filesystem) ||
                (candidate->vfs.backend_context != candidate) ||
                ((candidate->vfs.type != VFS_NODE_DIRECTORY) &&
                 (candidate->vfs.type != VFS_NODE_REGULAR))) {
                return NULL;
            }
            return candidate;
        }
    }
    return NULL;
}

static struct ramfs_node *ramfs_directory_from_vfs(struct ramfs *ramfs,
                                                   const struct vfs_node *node) {
    struct ramfs_node *const directory = ramfs_node_from_vfs(ramfs, node);

    if ((directory == NULL) ||
        (directory->vfs.type != VFS_NODE_DIRECTORY)) {
        return NULL;
    }
    return directory;
}

static struct ramfs_node *ramfs_regular_from_vfs(struct ramfs *ramfs,
                                                 const struct vfs_node *node) {
    struct ramfs_node *const regular = ramfs_node_from_vfs(ramfs, node);

    if ((regular == NULL) || (regular->vfs.type != VFS_NODE_REGULAR)) {
        return NULL;
    }
    return regular;
}

static enum vfs_result ramfs_validate_name(const char *name,
                                           size_t name_length) {
    size_t index;

    if ((name == NULL) || (name_length == 0U)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (name_length > (size_t)VFS_NAME_MAX) {
        return VFS_RESULT_NAME_TOO_LONG;
    }
    if (((name_length == 1U) && (name[0] == '.')) ||
        ((name_length == 2U) && (name[0] == '.') && (name[1] == '.'))) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    for (index = 0U; index < name_length; ++index) {
        if ((name[index] == '/') || (name[index] == '\0')) {
            return VFS_RESULT_INVALID_ARGUMENT;
        }
    }
    return VFS_RESULT_OK;
}

static bool ramfs_names_equal(const struct ramfs_node *node,
                              const char *name,
                              size_t name_length) {
    size_t index;

    if ((node == NULL) || (name == NULL) ||
        (node->name_length != name_length)) {
        return false;
    }
    for (index = 0U; index < name_length; ++index) {
        if (node->name[index] != name[index]) {
            return false;
        }
    }
    return true;
}

static struct ramfs_node *ramfs_find_child(struct ramfs *ramfs,
                                           const struct ramfs_node *directory,
                                           const char *name,
                                           size_t name_length) {
    size_t index;

    if ((ramfs == NULL) || (directory == NULL)) {
        return NULL;
    }
    for (index = 1U; index < (size_t)RAMFS_MAX_NODES; ++index) {
        struct ramfs_node *const candidate = &ramfs->nodes[index];
        if (candidate->occupied && candidate->vfs.valid &&
            (candidate->vfs.parent == &directory->vfs) &&
            ramfs_names_equal(candidate, name, name_length)) {
            return candidate;
        }
    }
    return NULL;
}

static bool ramfs_directory_has_children(struct ramfs *ramfs,
                                         const struct ramfs_node *directory) {
    size_t index;

    for (index = 1U; index < (size_t)RAMFS_MAX_NODES; ++index) {
        const struct ramfs_node *const candidate = &ramfs->nodes[index];
        if (candidate->occupied && candidate->vfs.valid &&
            (candidate->vfs.parent == &directory->vfs)) {
            return true;
        }
    }
    return false;
}

static struct ramfs_node *ramfs_find_free_node(struct ramfs *ramfs) {
    size_t index;

    for (index = 1U; index < (size_t)RAMFS_MAX_NODES; ++index) {
        if (!ramfs->nodes[index].occupied) {
            return &ramfs->nodes[index];
        }
    }
    return NULL;
}

static enum vfs_result ramfs_current_capacity(const struct ramfs *ramfs,
                                              uint64_t *capacity_out) {
    uint64_t total = 0ULL;
    size_t index;

    if ((ramfs == NULL) || (capacity_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    for (index = 0U; index < (size_t)RAMFS_MAX_NODES; ++index) {
        const struct ramfs_node *const node = &ramfs->nodes[index];
        if (!node->occupied) {
            continue;
        }
        if (node->capacity > (size_t)RAMFS_FILE_MAX) {
            return VFS_RESULT_CORRUPT;
        }
        if ((uint64_t)node->capacity > (UINT64_MAX - total)) {
            return VFS_RESULT_OVERFLOW;
        }
        total += (uint64_t)node->capacity;
    }
    *capacity_out = total;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_prepare_capacity(struct ramfs *ramfs,
                                              struct ramfs_node *node,
                                              uint64_t required_size,
                                              uint8_t **replacement_out,
                                              size_t *capacity_out) {
    uint64_t total_capacity;
    uint64_t without_old;
    enum vfs_result result;
    uint8_t *replacement;
    size_t required_capacity;

    if ((ramfs == NULL) || (node == NULL) || (replacement_out == NULL) ||
        (capacity_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *replacement_out = NULL;
    *capacity_out = node->capacity;
    if (required_size <= (uint64_t)node->capacity) {
        return VFS_RESULT_OK;
    }
    if (required_size > (uint64_t)RAMFS_FILE_MAX) {
        return VFS_RESULT_NO_SPACE;
    }
    if (required_size > (uint64_t)SIZE_MAX) {
        return VFS_RESULT_OVERFLOW;
    }
    required_capacity = (size_t)required_size;

    result = ramfs_current_capacity(ramfs, &total_capacity);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if (total_capacity < (uint64_t)node->capacity) {
        return VFS_RESULT_CORRUPT;
    }
    without_old = total_capacity - (uint64_t)node->capacity;
    if ((uint64_t)required_capacity >
        ((uint64_t)RAMFS_TOTAL_DATA_MAX - without_old)) {
        return VFS_RESULT_NO_SPACE;
    }

    replacement = (uint8_t *)kmalloc(required_capacity);
    if (replacement == NULL) {
        return VFS_RESULT_NO_SPACE;
    }
    ramfs_zero_bytes(replacement, required_capacity);
    if (node->size > (uint64_t)node->capacity) {
        (void)kfree(replacement);
        return VFS_RESULT_CORRUPT;
    }
    if ((node->data != NULL) && (node->size != 0ULL)) {
        ramfs_copy_bytes(replacement, node->data, (size_t)node->size);
    } else if ((node->data != NULL) != (node->capacity != 0U)) {
        (void)kfree(replacement);
        return VFS_RESULT_CORRUPT;
    }

    *replacement_out = replacement;
    *capacity_out = required_capacity;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_commit_replacement(struct ramfs_node *node,
                                                uint8_t *replacement,
                                                size_t replacement_capacity) {
    uint8_t *const old_data = node->data;

    if (replacement == NULL) {
        return VFS_RESULT_OK;
    }
    if ((old_data != NULL) && !kfree(old_data)) {
        (void)kfree(replacement);
        return VFS_RESULT_CORRUPT;
    }
    node->data = replacement;
    node->capacity = replacement_capacity;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_create_node(struct ramfs *ramfs,
                                         struct ramfs_node *directory,
                                         const char *name,
                                         size_t name_length,
                                         enum vfs_node_type type,
                                         struct vfs_node **node_out) {
    struct ramfs_node *slot;
    enum vfs_result result;
    size_t index;
    uint64_t node_id;

    if ((ramfs == NULL) || (directory == NULL) || (node_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *node_out = NULL;
    result = ramfs_validate_name(name, name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if (ramfs_find_child(ramfs, directory, name, name_length) != NULL) {
        return VFS_RESULT_ALREADY_EXISTS;
    }
    slot = ramfs_find_free_node(ramfs);
    if (slot == NULL) {
        return VFS_RESULT_NO_SPACE;
    }
    if (ramfs->next_node_id == UINT64_MAX) {
        return VFS_RESULT_OVERFLOW;
    }
    node_id = ramfs->next_node_id;

    ramfs_node_clear(slot);
    slot->owner = ramfs;
    slot->name_length = name_length;
    for (index = 0U; index < name_length; ++index) {
        slot->name[index] = name[index];
    }
    slot->name[name_length] = '\0';
    if (!vfs_node_prepare(&slot->vfs, &ramfs->filesystem, node_id, type,
                          &directory->vfs, slot)) {
        ramfs_node_clear(slot);
        return VFS_RESULT_CORRUPT;
    }
    slot->occupied = true;
    ++ramfs->next_node_id;
    *node_out = &slot->vfs;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_lookup(struct vfs_filesystem *filesystem,
                                    struct vfs_node *directory,
                                    const char *name,
                                    size_t name_length,
                                    struct vfs_node **node_out) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *parent;
    struct ramfs_node *child;
    enum vfs_result result;

    if ((ramfs == NULL) || (node_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *node_out = NULL;
    parent = ramfs_directory_from_vfs(ramfs, directory);
    if (parent == NULL) {
        return (ramfs_node_from_vfs(ramfs, directory) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_DIRECTORY;
    }
    result = ramfs_validate_name(name, name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    child = ramfs_find_child(ramfs, parent, name, name_length);
    if (child == NULL) {
        return VFS_RESULT_NOT_FOUND;
    }
    *node_out = &child->vfs;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_create(struct vfs_filesystem *filesystem,
                                    struct vfs_node *directory,
                                    const char *name,
                                    size_t name_length,
                                    struct vfs_node **node_out) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *const parent = ramfs_directory_from_vfs(ramfs,
                                                               directory);

    if (ramfs == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (parent == NULL) {
        return (ramfs_node_from_vfs(ramfs, directory) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_DIRECTORY;
    }
    return ramfs_create_node(ramfs, parent, name, name_length,
                             VFS_NODE_REGULAR, node_out);
}

static enum vfs_result ramfs_mkdir(struct vfs_filesystem *filesystem,
                                   struct vfs_node *directory,
                                   const char *name,
                                   size_t name_length,
                                   struct vfs_node **node_out) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *const parent = ramfs_directory_from_vfs(ramfs,
                                                               directory);

    if (ramfs == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (parent == NULL) {
        return (ramfs_node_from_vfs(ramfs, directory) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_DIRECTORY;
    }
    return ramfs_create_node(ramfs, parent, name, name_length,
                             VFS_NODE_DIRECTORY, node_out);
}

static enum vfs_result ramfs_remove_node(struct ramfs_node *node) {
    uint8_t *data;

    if ((node == NULL) || !node->occupied || !node->vfs.valid) {
        return VFS_RESULT_CORRUPT;
    }
    if (node->vfs.reference_count != 0U) {
        return VFS_RESULT_BUSY;
    }
    data = node->data;
    if ((data != NULL) && !kfree(data)) {
        return VFS_RESULT_CORRUPT;
    }
    ramfs_node_clear(node);
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_unlink(struct vfs_filesystem *filesystem,
                                    struct vfs_node *directory,
                                    const char *name,
                                    size_t name_length) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *parent;
    struct ramfs_node *child;
    enum vfs_result result;

    if (ramfs == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    parent = ramfs_directory_from_vfs(ramfs, directory);
    if (parent == NULL) {
        return (ramfs_node_from_vfs(ramfs, directory) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_DIRECTORY;
    }
    result = ramfs_validate_name(name, name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    child = ramfs_find_child(ramfs, parent, name, name_length);
    if (child == NULL) {
        return VFS_RESULT_NOT_FOUND;
    }
    if (child->vfs.type != VFS_NODE_REGULAR) {
        return VFS_RESULT_NOT_REGULAR;
    }
    return ramfs_remove_node(child);
}

static enum vfs_result ramfs_rmdir(struct vfs_filesystem *filesystem,
                                   struct vfs_node *directory,
                                   const char *name,
                                   size_t name_length) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *parent;
    struct ramfs_node *child;
    enum vfs_result result;

    if (ramfs == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    parent = ramfs_directory_from_vfs(ramfs, directory);
    if (parent == NULL) {
        return (ramfs_node_from_vfs(ramfs, directory) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_DIRECTORY;
    }
    result = ramfs_validate_name(name, name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    child = ramfs_find_child(ramfs, parent, name, name_length);
    if (child == NULL) {
        return VFS_RESULT_NOT_FOUND;
    }
    if (child->vfs.type != VFS_NODE_DIRECTORY) {
        return VFS_RESULT_NOT_DIRECTORY;
    }
    if (ramfs_directory_has_children(ramfs, child)) {
        return VFS_RESULT_NOT_EMPTY;
    }
    return ramfs_remove_node(child);
}

static enum vfs_result ramfs_validate_rename_cycle(
    struct ramfs *ramfs,
    const struct ramfs_node *source,
    const struct ramfs_node *new_directory) {
    const struct ramfs_node *cursor = new_directory;
    size_t steps;

    if (source->vfs.type != VFS_NODE_DIRECTORY) {
        return VFS_RESULT_OK;
    }
    for (steps = 0U; steps < (size_t)RAMFS_MAX_NODES; ++steps) {
        if (cursor == source) {
            return VFS_RESULT_INVALID_ARGUMENT;
        }
        if (&cursor->vfs == ramfs->filesystem.root) {
            return VFS_RESULT_OK;
        }
        cursor = ramfs_directory_from_vfs(ramfs, cursor->vfs.parent);
        if (cursor == NULL) {
            return VFS_RESULT_CORRUPT;
        }
    }
    return VFS_RESULT_CORRUPT;
}

static enum vfs_result ramfs_rename(struct vfs_filesystem *filesystem,
                                    struct vfs_node *old_directory,
                                    const char *old_name,
                                    size_t old_name_length,
                                    struct vfs_node *new_directory,
                                    const char *new_name,
                                    size_t new_name_length) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *old_parent;
    struct ramfs_node *new_parent;
    struct ramfs_node *source;
    struct ramfs_node *collision;
    enum vfs_result result;
    size_t index;

    if (ramfs == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    old_parent = ramfs_directory_from_vfs(ramfs, old_directory);
    new_parent = ramfs_directory_from_vfs(ramfs, new_directory);
    if ((old_parent == NULL) || (new_parent == NULL)) {
        return VFS_RESULT_NOT_DIRECTORY;
    }
    result = ramfs_validate_name(old_name, old_name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = ramfs_validate_name(new_name, new_name_length);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    source = ramfs_find_child(ramfs, old_parent, old_name, old_name_length);
    if (source == NULL) {
        return VFS_RESULT_NOT_FOUND;
    }
    collision = ramfs_find_child(ramfs, new_parent, new_name,
                                 new_name_length);
    if (collision != NULL) {
        if ((collision == source) && (old_parent == new_parent) &&
            ramfs_names_equal(source, new_name, new_name_length)) {
            return VFS_RESULT_OK;
        }
        return VFS_RESULT_ALREADY_EXISTS;
    }
    result = ramfs_validate_rename_cycle(ramfs, source, new_parent);
    if (result != VFS_RESULT_OK) {
        return result;
    }

    for (index = 0U; index <= (size_t)VFS_NAME_MAX; ++index) {
        source->name[index] = '\0';
    }
    for (index = 0U; index < new_name_length; ++index) {
        source->name[index] = new_name[index];
    }
    source->name_length = new_name_length;
    source->vfs.parent = &new_parent->vfs;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_read(struct vfs_filesystem *filesystem,
                                  struct vfs_node *node,
                                  uint64_t offset,
                                  void *buffer,
                                  size_t length,
                                  size_t *transferred_out) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *regular;
    uint8_t *const output = (uint8_t *)buffer;
    uint64_t available;
    size_t transferred;

    if ((ramfs == NULL) || (buffer == NULL) || (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *transferred_out = 0U;
    regular = ramfs_regular_from_vfs(ramfs, node);
    if (regular == NULL) {
        return (ramfs_node_from_vfs(ramfs, node) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_REGULAR;
    }
    if (length > (size_t)VFS_IO_MAX) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (regular->size > (uint64_t)regular->capacity) {
        return VFS_RESULT_CORRUPT;
    }
    if ((offset >= regular->size) || (length == 0U)) {
        return VFS_RESULT_OK;
    }
    available = regular->size - offset;
    transferred = (available < (uint64_t)length) ? (size_t)available : length;
    if ((regular->data == NULL) || (offset > (uint64_t)SIZE_MAX)) {
        return VFS_RESULT_CORRUPT;
    }
    ramfs_copy_bytes(output, &regular->data[(size_t)offset], transferred);
    *transferred_out = transferred;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_write(struct vfs_filesystem *filesystem,
                                   struct vfs_node *node,
                                   uint64_t offset,
                                   const void *buffer,
                                   size_t length,
                                   size_t *transferred_out) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *regular;
    const uint8_t *const input = (const uint8_t *)buffer;
    uint64_t end;
    uint64_t new_size;
    uint8_t *replacement = NULL;
    uint8_t *target;
    size_t replacement_capacity;
    enum vfs_result result;

    if ((ramfs == NULL) || (buffer == NULL) || (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *transferred_out = 0U;
    regular = ramfs_regular_from_vfs(ramfs, node);
    if (regular == NULL) {
        return (ramfs_node_from_vfs(ramfs, node) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_REGULAR;
    }
    if (length > (size_t)VFS_IO_MAX) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (length == 0U) {
        return VFS_RESULT_OK;
    }
    if ((uint64_t)length > (UINT64_MAX - offset)) {
        return VFS_RESULT_OVERFLOW;
    }
    end = offset + (uint64_t)length;
    if (end > (uint64_t)RAMFS_FILE_MAX) {
        return VFS_RESULT_NO_SPACE;
    }
    new_size = (end > regular->size) ? end : regular->size;
    result = ramfs_prepare_capacity(ramfs, regular, new_size,
                                    &replacement, &replacement_capacity);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    target = (replacement != NULL) ? replacement : regular->data;
    if ((target == NULL) || (offset > (uint64_t)SIZE_MAX) ||
        (new_size > (uint64_t)SIZE_MAX)) {
        if (replacement != NULL) {
            (void)kfree(replacement);
        }
        return VFS_RESULT_CORRUPT;
    }
    if (offset > regular->size) {
        ramfs_zero_bytes(&target[(size_t)regular->size],
                         (size_t)(offset - regular->size));
    }
    ramfs_copy_bytes(&target[(size_t)offset], input, length);

    result = ramfs_commit_replacement(regular, replacement,
                                      replacement_capacity);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    regular->size = new_size;
    *transferred_out = length;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_truncate(struct vfs_filesystem *filesystem,
                                      struct vfs_node *node,
                                      uint64_t size) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *regular;
    uint8_t *replacement = NULL;
    size_t replacement_capacity;
    enum vfs_result result;

    if (ramfs == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    regular = ramfs_regular_from_vfs(ramfs, node);
    if (regular == NULL) {
        return (ramfs_node_from_vfs(ramfs, node) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_REGULAR;
    }
    if (size > (uint64_t)RAMFS_FILE_MAX) {
        return VFS_RESULT_NO_SPACE;
    }
    if (size == 0ULL) {
        if ((regular->data != NULL) && !kfree(regular->data)) {
            return VFS_RESULT_CORRUPT;
        }
        regular->data = NULL;
        regular->capacity = 0U;
        regular->size = 0ULL;
        return VFS_RESULT_OK;
    }
    if (size <= regular->size) {
        regular->size = size;
        return VFS_RESULT_OK;
    }

    result = ramfs_prepare_capacity(ramfs, regular, size,
                                    &replacement, &replacement_capacity);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    if (replacement != NULL) {
        ramfs_zero_bytes(&replacement[(size_t)regular->size],
                         (size_t)(size - regular->size));
    } else if (regular->data != NULL) {
        ramfs_zero_bytes(&regular->data[(size_t)regular->size],
                         (size_t)(size - regular->size));
    } else {
        return VFS_RESULT_CORRUPT;
    }
    result = ramfs_commit_replacement(regular, replacement,
                                      replacement_capacity);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    regular->size = size;
    return VFS_RESULT_OK;
}

static enum vfs_result ramfs_readdir(struct vfs_filesystem *filesystem,
                                     struct vfs_node *directory,
                                     uint64_t index,
                                     struct vfs_dirent *entry_out) {
    struct ramfs *const ramfs = ramfs_instance_from_filesystem(filesystem);
    struct ramfs_node *parent;
    uint64_t live_index = 0ULL;
    size_t slot;
    size_t name_index;

    if ((ramfs == NULL) || (entry_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    parent = ramfs_directory_from_vfs(ramfs, directory);
    if (parent == NULL) {
        return (ramfs_node_from_vfs(ramfs, directory) == NULL)
                   ? VFS_RESULT_CORRUPT
                   : VFS_RESULT_NOT_DIRECTORY;
    }

    for (slot = 1U; slot < (size_t)RAMFS_MAX_NODES; ++slot) {
        struct ramfs_node *const child = &ramfs->nodes[slot];
        if (!child->occupied || !child->vfs.valid ||
            (child->vfs.parent != &parent->vfs)) {
            continue;
        }
        if (live_index != index) {
            ++live_index;
            continue;
        }
        entry_out->node_id = child->vfs.id;
        entry_out->type = child->vfs.type;
        entry_out->name_length = child->name_length;
        for (name_index = 0U; name_index < child->name_length;
             ++name_index) {
            entry_out->name[name_index] = child->name[name_index];
        }
        entry_out->name[child->name_length] = '\0';
        return VFS_RESULT_OK;
    }
    return VFS_RESULT_NOT_FOUND;
}

enum vfs_result ramfs_create_filesystem(uint64_t filesystem_id,
                                        struct ramfs **ramfs_out) {
    struct ramfs *ramfs;
    struct ramfs_node *root;
    size_t registry_slot = (size_t)RAMFS_MAX_FILESYSTEMS;
    size_t index;

    if ((filesystem_id == 0ULL) || (ramfs_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *ramfs_out = NULL;
    for (index = 0U; index < (size_t)RAMFS_MAX_FILESYSTEMS; ++index) {
        if (ramfs_instances[index] == NULL) {
            if (registry_slot == (size_t)RAMFS_MAX_FILESYSTEMS) {
                registry_slot = index;
            }
        } else if (ramfs_instances[index]->filesystem.id == filesystem_id) {
            return VFS_RESULT_ALREADY_EXISTS;
        }
    }
    if (registry_slot == (size_t)RAMFS_MAX_FILESYSTEMS) {
        return VFS_RESULT_NO_SPACE;
    }

    ramfs = (struct ramfs *)kmalloc(sizeof(*ramfs));
    if (ramfs == NULL) {
        return VFS_RESULT_NO_SPACE;
    }
    ramfs->filesystem.id = 0ULL;
    ramfs->filesystem.operations = NULL;
    ramfs->filesystem.root = NULL;
    ramfs->filesystem.backend_context = NULL;
    ramfs->filesystem.valid = false;
    ramfs->next_node_id = 0ULL;
    ramfs->registered = false;
    for (index = 0U; index < (size_t)RAMFS_MAX_NODES; ++index) {
        ramfs_node_clear(&ramfs->nodes[index]);
    }

    if (!vfs_filesystem_prepare(&ramfs->filesystem, filesystem_id,
                                &ramfs_operations, ramfs)) {
        (void)kfree(ramfs);
        return VFS_RESULT_CORRUPT;
    }
    root = &ramfs->nodes[0];
    root->owner = ramfs;
    root->name[0] = '\0';
    root->name_length = 0U;
    if (!vfs_node_prepare(&root->vfs, &ramfs->filesystem, 1ULL,
                          VFS_NODE_DIRECTORY, NULL, root) ||
        !vfs_filesystem_set_root(&ramfs->filesystem, &root->vfs)) {
        ramfs->filesystem.valid = false;
        (void)kfree(ramfs);
        return VFS_RESULT_CORRUPT;
    }
    root->occupied = true;
    ramfs->next_node_id = 2ULL;
    ramfs->registered = true;
    ramfs_instances[registry_slot] = ramfs;
    *ramfs_out = ramfs;
    return VFS_RESULT_OK;
}

struct vfs_filesystem *ramfs_get_vfs(struct ramfs *ramfs) {
    struct ramfs *const known = ramfs_instance_from_pointer(ramfs);

    if ((known == NULL) || !known->registered || !known->filesystem.valid) {
        return NULL;
    }
    return &known->filesystem;
}

enum vfs_result ramfs_get_stats(const struct ramfs *ramfs,
                                struct ramfs_stats *stats_out) {
    struct ramfs *const known = ramfs_instance_from_pointer(ramfs);
    size_t index;

    if ((known == NULL) || (stats_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    stats_out->live_nodes = 0ULL;
    stats_out->live_directories = 0ULL;
    stats_out->live_files = 0ULL;
    stats_out->live_file_bytes = 0ULL;
    stats_out->allocated_file_capacity = 0ULL;

    for (index = 0U; index < (size_t)RAMFS_MAX_NODES; ++index) {
        const struct ramfs_node *const node = &known->nodes[index];
        if (!node->occupied) {
            continue;
        }
        if (!node->vfs.valid || (node->owner != known) ||
            (node->vfs.filesystem != &known->filesystem) ||
            (node->vfs.backend_context != node)) {
            return VFS_RESULT_CORRUPT;
        }
        ++stats_out->live_nodes;
        if (node->vfs.type == VFS_NODE_DIRECTORY) {
            ++stats_out->live_directories;
        } else if (node->vfs.type == VFS_NODE_REGULAR) {
            ++stats_out->live_files;
            if (node->size > (UINT64_MAX - stats_out->live_file_bytes) ||
                (uint64_t)node->capacity >
                    (UINT64_MAX - stats_out->allocated_file_capacity)) {
                return VFS_RESULT_OVERFLOW;
            }
            stats_out->live_file_bytes += node->size;
            stats_out->allocated_file_capacity += (uint64_t)node->capacity;
        } else {
            return VFS_RESULT_CORRUPT;
        }
    }
    return VFS_RESULT_OK;
}

enum vfs_result ramfs_destroy_filesystem(struct ramfs *ramfs) {
    struct ramfs *const known = ramfs_instance_from_pointer(ramfs);
    size_t registry_index;
    size_t node_index;

    if (known == NULL) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    for (node_index = 0U; node_index < (size_t)RAMFS_MAX_NODES;
         ++node_index) {
        const struct ramfs_node *const node = &known->nodes[node_index];
        if (node->occupied && (node->vfs.reference_count != 0U)) {
            return VFS_RESULT_BUSY;
        }
    }

    for (node_index = 0U; node_index < (size_t)RAMFS_MAX_NODES;
         ++node_index) {
        struct ramfs_node *const node = &known->nodes[node_index];
        if (node->occupied && (node->data != NULL) && !kfree(node->data)) {
            return VFS_RESULT_CORRUPT;
        }
        ramfs_node_clear(node);
    }
    known->filesystem.id = 0ULL;
    known->filesystem.operations = NULL;
    known->filesystem.root = NULL;
    known->filesystem.backend_context = NULL;
    known->filesystem.valid = false;
    known->next_node_id = 0ULL;
    known->registered = false;

    for (registry_index = 0U;
         registry_index < (size_t)RAMFS_MAX_FILESYSTEMS;
         ++registry_index) {
        if (ramfs_instances[registry_index] == known) {
            ramfs_instances[registry_index] = NULL;
            break;
        }
    }
    if (registry_index == (size_t)RAMFS_MAX_FILESYSTEMS) {
        return VFS_RESULT_CORRUPT;
    }
    if (!kfree(known)) {
        return VFS_RESULT_CORRUPT;
    }
    return VFS_RESULT_OK;
}
