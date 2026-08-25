#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/boringfs.h>
#include <boring/boringfs_vfs.h>
#include <boring/heap.h>
#include <boring/vfs.h>

#define BORINGFS_DEVICE_SECTOR_SIZE 512U
#define BORINGFS_SECTORS_PER_BLOCK \
    (BORINGFS_BLOCK_SIZE / BORINGFS_DEVICE_SECTOR_SIZE)
#define BORINGFS_CACHE_INVALID_BLOCK UINT32_MAX

struct boringfs_cached_node {
    struct vfs_node vfs;
    bool prepared;
};

struct boringfs_vfs {
    struct vfs_filesystem filesystem;
    const struct block_device *device;
    struct boringfs_superblock superblock;
    struct boringfs_source source;
    struct boringfs_cached_node *nodes;
    uint32_t node_count;
    uint8_t block_cache[BORINGFS_BLOCK_SIZE];
    uint32_t cached_block;
    bool cache_valid;
};

static enum vfs_result boringfs_lookup(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length,
                                       struct vfs_node **node_out);
static enum vfs_result boringfs_create(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length,
                                       struct vfs_node **node_out);
static enum vfs_result boringfs_mkdir(struct vfs_filesystem *filesystem,
                                      struct vfs_node *directory,
                                      const char *name,
                                      size_t name_length,
                                      struct vfs_node **node_out);
static enum vfs_result boringfs_unlink(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length);
static enum vfs_result boringfs_rmdir(struct vfs_filesystem *filesystem,
                                      struct vfs_node *directory,
                                      const char *name,
                                      size_t name_length);
static enum vfs_result boringfs_rename(struct vfs_filesystem *filesystem,
                                       struct vfs_node *old_directory,
                                       const char *old_name,
                                       size_t old_name_length,
                                       struct vfs_node *new_directory,
                                       const char *new_name,
                                       size_t new_name_length);
static enum vfs_result boringfs_read(struct vfs_filesystem *filesystem,
                                     struct vfs_node *node,
                                     uint64_t offset,
                                     void *buffer,
                                     size_t length,
                                     size_t *transferred_out);
static enum vfs_result boringfs_write(struct vfs_filesystem *filesystem,
                                      struct vfs_node *node,
                                      uint64_t offset,
                                      const void *buffer,
                                      size_t length,
                                      size_t *transferred_out);
static enum vfs_result boringfs_truncate(struct vfs_filesystem *filesystem,
                                         struct vfs_node *node,
                                         uint64_t size);
static enum vfs_result boringfs_readdir(struct vfs_filesystem *filesystem,
                                        struct vfs_node *directory,
                                        uint64_t index,
                                        struct vfs_dirent *entry_out);

static const struct vfs_operations boringfs_operations = {
    .lookup = boringfs_lookup,
    .create = boringfs_create,
    .mkdir = boringfs_mkdir,
    .unlink = boringfs_unlink,
    .rmdir = boringfs_rmdir,
    .rename = boringfs_rename,
    .read = boringfs_read,
    .write = boringfs_write,
    .truncate = boringfs_truncate,
    .readdir = boringfs_readdir
};

static bool checked_add_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if ((out == NULL) || (left > UINT64_MAX - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool checked_mul_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if ((out == NULL) || ((left != 0ULL) && (right > UINT64_MAX / left))) {
        return false;
    }
    *out = left * right;
    return true;
}

static void copy_bytes(uint8_t *destination,
                       const uint8_t *source,
                       size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static void zero_bytes(uint8_t *destination, size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = 0U;
    }
}

static bool bytes_all_zero(const uint8_t *bytes, size_t length) {
    size_t index;

    if (bytes == NULL) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool names_equal(const uint8_t *disk_name,
                        size_t disk_length,
                        const char *name,
                        size_t name_length) {
    size_t index;

    if ((disk_name == NULL) || (name == NULL) ||
        (disk_length != name_length)) {
        return false;
    }
    for (index = 0U; index < name_length; ++index) {
        if (disk_name[index] != (uint8_t)name[index]) {
            return false;
        }
    }
    return true;
}

static bool map_fs_blocks_to_sectors(uint32_t first_fs_block,
                                     uint32_t fs_block_count,
                                     uint64_t *first_sector_out,
                                     uint64_t *sector_count_out) {
    uint64_t first_sector;
    uint64_t sector_count;

    if ((first_sector_out == NULL) || (sector_count_out == NULL) ||
        !checked_mul_u64((uint64_t)first_fs_block,
                         (uint64_t)BORINGFS_SECTORS_PER_BLOCK,
                         &first_sector) ||
        !checked_mul_u64((uint64_t)fs_block_count,
                         (uint64_t)BORINGFS_SECTORS_PER_BLOCK,
                         &sector_count)) {
        return false;
    }
    *first_sector_out = first_sector;
    *sector_count_out = sector_count;
    return true;
}

static bool load_cached_block(struct boringfs_vfs *boringfs,
                              uint32_t fs_block) {
    uint64_t first_sector;
    uint64_t sector_count;

    if ((boringfs == NULL) || (fs_block >= boringfs->superblock.total_blocks) ||
        !map_fs_blocks_to_sectors(fs_block, 1U,
                                  &first_sector, &sector_count) ||
        (sector_count > (uint64_t)UINT32_MAX) ||
        (block_device_read(boringfs->device, first_sector,
                           (uint32_t)sector_count,
                           boringfs->block_cache) != BLOCK_DEVICE_RESULT_OK)) {
        return false;
    }
    boringfs->cached_block = fs_block;
    boringfs->cache_valid = true;
    return true;
}

static bool boringfs_source_read(void *context,
                                 uint64_t offset,
                                 void *buffer,
                                 size_t length) {
    struct boringfs_vfs *const boringfs = (struct boringfs_vfs *)context;
    uint8_t *destination = (uint8_t *)buffer;
    size_t remaining = length;
    uint64_t current = offset;

    if ((boringfs == NULL) ||
        ((destination == NULL) && (length != 0U)) ||
        (offset > boringfs->source.size) ||
        ((uint64_t)length > boringfs->source.size - offset)) {
        return false;
    }

    while (remaining != 0U) {
        const uint64_t fs_block64 = current / (uint64_t)BORINGFS_BLOCK_SIZE;
        const size_t in_block =
            (size_t)(current % (uint64_t)BORINGFS_BLOCK_SIZE);
        size_t chunk = (size_t)BORINGFS_BLOCK_SIZE - in_block;

        if ((fs_block64 > (uint64_t)UINT32_MAX) ||
            (fs_block64 >= (uint64_t)boringfs->superblock.total_blocks)) {
            return false;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }
        if (!boringfs->cache_valid ||
            (boringfs->cached_block != (uint32_t)fs_block64)) {
            if (!load_cached_block(boringfs, (uint32_t)fs_block64)) {
                return false;
            }
        }
        copy_bytes(destination, &boringfs->block_cache[in_block], chunk);
        destination += chunk;
        remaining -= chunk;
        current += (uint64_t)chunk;
    }
    return true;
}

static struct boringfs_vfs *context_from_filesystem(
    const struct vfs_filesystem *filesystem) {
    struct boringfs_vfs *boringfs;

    if ((filesystem == NULL) || !filesystem->valid ||
        (filesystem->operations != &boringfs_operations) ||
        (filesystem->backend_context == NULL)) {
        return NULL;
    }
    boringfs = (struct boringfs_vfs *)filesystem->backend_context;
    if (&boringfs->filesystem != filesystem) {
        return NULL;
    }
    return boringfs;
}

static bool decode_object(struct boringfs_vfs *boringfs,
                          uint32_t object_id,
                          struct boringfs_object *object_out,
                          uint8_t raw[BORINGFS_OBJECT_RECORD_SIZE]) {
    uint64_t table_base;
    uint64_t slot_offset;
    uint64_t absolute;

    if ((boringfs == NULL) || (object_out == NULL) || (raw == NULL) ||
        (object_id == BORINGFS_NULL_OBJECT_ID) ||
        (object_id > boringfs->superblock.object_count) ||
        !checked_mul_u64((uint64_t)boringfs->superblock.object_table_start,
                         (uint64_t)BORINGFS_BLOCK_SIZE, &table_base) ||
        !checked_mul_u64((uint64_t)(object_id - 1U),
                         (uint64_t)BORINGFS_OBJECT_RECORD_SIZE,
                         &slot_offset) ||
        !checked_add_u64(table_base, slot_offset, &absolute) ||
        !boringfs_source_read(boringfs, absolute, raw,
                              (size_t)BORINGFS_OBJECT_RECORD_SIZE) ||
        !boringfs_decode_object(raw,
                                (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                object_out)) {
        return false;
    }
    return true;
}

static bool object_type_to_vfs(uint8_t type, enum vfs_node_type *type_out) {
    if (type_out == NULL) {
        return false;
    }
    if (type == BORINGFS_TYPE_DIRECTORY) {
        *type_out = VFS_NODE_DIRECTORY;
        return true;
    }
    if (type == BORINGFS_TYPE_REGULAR) {
        *type_out = VFS_NODE_REGULAR;
        return true;
    }
    return false;
}

static enum vfs_result prepare_node(struct boringfs_vfs *boringfs,
                                    uint32_t object_id,
                                    struct vfs_node *parent,
                                    struct vfs_node **node_out) {
    struct boringfs_cached_node *cached;
    struct boringfs_object object;
    uint8_t raw[BORINGFS_OBJECT_RECORD_SIZE];
    enum vfs_node_type type;

    if ((boringfs == NULL) || (node_out == NULL) ||
        (object_id == BORINGFS_NULL_OBJECT_ID) ||
        (object_id > boringfs->node_count)) {
        return VFS_RESULT_CORRUPT;
    }
    *node_out = NULL;
    cached = &boringfs->nodes[(size_t)(object_id - 1U)];
    if (cached->prepared) {
        if (!cached->vfs.valid ||
            (cached->vfs.id != (uint64_t)object_id) ||
            (cached->vfs.filesystem != &boringfs->filesystem) ||
            ((object_id != BORINGFS_ROOT_OBJECT_ID) &&
             (cached->vfs.parent != parent))) {
            return VFS_RESULT_CORRUPT;
        }
        *node_out = &cached->vfs;
        return VFS_RESULT_OK;
    }
    if (!decode_object(boringfs, object_id, &object, raw) ||
        bytes_all_zero(raw, (size_t)BORINGFS_OBJECT_RECORD_SIZE) ||
        (object.state != BORINGFS_OBJECT_ALLOCATED) ||
        (object.object_id != object_id) ||
        !object_type_to_vfs(object.type, &type)) {
        return VFS_RESULT_CORRUPT;
    }
    if ((object_id != BORINGFS_ROOT_OBJECT_ID) && (parent == NULL)) {
        return VFS_RESULT_CORRUPT;
    }
    if (!vfs_node_prepare(&cached->vfs, &boringfs->filesystem,
                          (uint64_t)object_id, type, parent, cached)) {
        return VFS_RESULT_CORRUPT;
    }
    cached->prepared = true;
    *node_out = &cached->vfs;
    return VFS_RESULT_OK;
}

static bool node_object_id(struct boringfs_vfs *boringfs,
                           const struct vfs_node *node,
                           uint32_t *object_id_out) {
    uint64_t id;
    struct boringfs_cached_node *cached;

    if ((boringfs == NULL) || (node == NULL) || (object_id_out == NULL) ||
        (node->filesystem != &boringfs->filesystem) || !node->valid) {
        return false;
    }
    id = node->id;
    if ((id == 0ULL) || (id > (uint64_t)boringfs->node_count)) {
        return false;
    }
    cached = &boringfs->nodes[(size_t)(id - 1ULL)];
    if (!cached->prepared || (&cached->vfs != node)) {
        return false;
    }
    *object_id_out = (uint32_t)id;
    return true;
}

static bool object_read_exact(struct boringfs_vfs *boringfs,
                              const struct boringfs_object *object,
                              uint64_t logical_offset,
                              void *buffer,
                              size_t length) {
    uint64_t offset = logical_offset;
    uint8_t *destination = (uint8_t *)buffer;
    size_t remaining = length;
    size_t extent_index;

    if ((boringfs == NULL) || (object == NULL) ||
        ((destination == NULL) && (length != 0U)) ||
        (object->extent_count > BORINGFS_MAX_EXTENTS)) {
        return false;
    }
    for (extent_index = 0U;
         (extent_index < (size_t)object->extent_count) && (remaining != 0U);
         ++extent_index) {
        const struct boringfs_extent *const extent =
            &object->extents[extent_index];
        uint64_t extent_bytes;
        uint64_t end_block;

        if ((extent->block_count == 0U) ||
            (extent->start_block < boringfs->superblock.data_start) ||
            !checked_add_u64((uint64_t)extent->start_block,
                             (uint64_t)extent->block_count, &end_block) ||
            (end_block > (uint64_t)boringfs->superblock.total_blocks) ||
            !checked_mul_u64((uint64_t)extent->block_count,
                             (uint64_t)BORINGFS_BLOCK_SIZE,
                             &extent_bytes)) {
            return false;
        }
        if (offset >= extent_bytes) {
            offset -= extent_bytes;
            continue;
        }
        while ((offset < extent_bytes) && (remaining != 0U)) {
            uint64_t physical_base;
            uint64_t physical;
            const uint64_t available64 = extent_bytes - offset;
            size_t chunk = remaining;

            if ((available64 < (uint64_t)chunk)) {
                chunk = (size_t)available64;
            }
            if (!checked_mul_u64((uint64_t)extent->start_block,
                                 (uint64_t)BORINGFS_BLOCK_SIZE,
                                 &physical_base) ||
                !checked_add_u64(physical_base, offset, &physical) ||
                !boringfs_source_read(boringfs, physical,
                                      destination, chunk)) {
                return false;
            }
            destination += chunk;
            remaining -= chunk;
            offset += (uint64_t)chunk;
        }
        offset = 0ULL;
    }
    return remaining == 0U;
}

static enum vfs_result validate_directory_record(
    struct boringfs_vfs *boringfs,
    uint32_t directory_id,
    const uint8_t raw[BORINGFS_DIRECTORY_RECORD_SIZE],
    struct boringfs_directory_record *record_out,
    struct boringfs_object *target_out) {
    struct boringfs_directory_record record;
    struct boringfs_object target;
    uint8_t object_raw[BORINGFS_OBJECT_RECORD_SIZE];
    size_t index;

    if ((boringfs == NULL) || (raw == NULL) || (record_out == NULL) ||
        (target_out == NULL) ||
        !boringfs_decode_directory_record(
            raw, (size_t)BORINGFS_DIRECTORY_RECORD_SIZE, &record) ||
        (record.object_id == BORINGFS_NULL_OBJECT_ID) ||
        (record.object_id > boringfs->superblock.object_count) ||
        (record.name_length == 0U) ||
        (record.name_length > BORINGFS_MAX_FILENAME) ||
        ((record.type_hint != BORINGFS_TYPE_REGULAR) &&
         (record.type_hint != BORINGFS_TYPE_DIRECTORY)) ||
        (record.flags != 0U) || !bytes_all_zero(&raw[248], 8U) ||
        !boringfs_utf8_valid(record.name, (size_t)record.name_length)) {
        return VFS_RESULT_CORRUPT;
    }
    if (((record.name_length == 1U) && (record.name[0] == (uint8_t)'.')) ||
        ((record.name_length == 2U) &&
         (record.name[0] == (uint8_t)'.') &&
         (record.name[1] == (uint8_t)'.'))) {
        return VFS_RESULT_CORRUPT;
    }
    for (index = 0U; index < (size_t)record.name_length; ++index) {
        if ((record.name[index] == 0U) ||
            (record.name[index] == (uint8_t)'/')) {
            return VFS_RESULT_CORRUPT;
        }
    }
    for (index = (size_t)record.name_length;
         index < (size_t)BORINGFS_MAX_FILENAME; ++index) {
        if (record.name[index] != 0U) {
            return VFS_RESULT_CORRUPT;
        }
    }
    if (!decode_object(boringfs, record.object_id, &target, object_raw) ||
        (target.state != BORINGFS_OBJECT_ALLOCATED) ||
        (target.object_id != record.object_id) ||
        (target.parent_object_id != directory_id) ||
        (target.type != record.type_hint)) {
        return VFS_RESULT_CORRUPT;
    }
    *record_out = record;
    *target_out = target;
    return VFS_RESULT_OK;
}

static enum vfs_result read_directory_slot(
    struct boringfs_vfs *boringfs,
    uint32_t directory_id,
    uint64_t slot,
    uint8_t raw[BORINGFS_DIRECTORY_RECORD_SIZE],
    struct boringfs_directory_record *record_out,
    struct boringfs_object *target_out,
    bool *occupied_out) {
    struct boringfs_object directory;
    uint8_t object_raw[BORINGFS_OBJECT_RECORD_SIZE];
    uint64_t record_count;
    uint64_t logical_offset;

    if ((boringfs == NULL) || (raw == NULL) || (record_out == NULL) ||
        (target_out == NULL) || (occupied_out == NULL) ||
        !decode_object(boringfs, directory_id, &directory, object_raw) ||
        (directory.state != BORINGFS_OBJECT_ALLOCATED) ||
        (directory.type != BORINGFS_TYPE_DIRECTORY) ||
        ((directory.size_bytes %
          (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE) != 0ULL)) {
        return VFS_RESULT_CORRUPT;
    }
    record_count = directory.size_bytes /
                   (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE;
    if (slot >= record_count) {
        return VFS_RESULT_NOT_FOUND;
    }
    if (!checked_mul_u64(slot, (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                         &logical_offset) ||
        !object_read_exact(boringfs, &directory, logical_offset, raw,
                           (size_t)BORINGFS_DIRECTORY_RECORD_SIZE)) {
        return VFS_RESULT_CORRUPT;
    }
    if (bytes_all_zero(raw, (size_t)BORINGFS_DIRECTORY_RECORD_SIZE)) {
        *occupied_out = false;
        return VFS_RESULT_OK;
    }
    *occupied_out = true;
    return validate_directory_record(boringfs, directory_id, raw,
                                     record_out, target_out);
}

static enum vfs_result boringfs_lookup(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length,
                                       struct vfs_node **node_out) {
    struct boringfs_vfs *const boringfs = context_from_filesystem(filesystem);
    uint32_t directory_id;
    struct boringfs_object directory_object;
    uint8_t directory_raw[BORINGFS_OBJECT_RECORD_SIZE];
    uint64_t record_count;
    uint64_t slot;

    if ((boringfs == NULL) || (name == NULL) || (node_out == NULL) ||
        !node_object_id(boringfs, directory, &directory_id)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *node_out = NULL;
    if (directory->type != VFS_NODE_DIRECTORY) {
        return VFS_RESULT_NOT_DIRECTORY;
    }
    if (name_length == 0U) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (name_length > (size_t)BORINGFS_MAX_FILENAME) {
        return VFS_RESULT_NAME_TOO_LONG;
    }
    if (!decode_object(boringfs, directory_id, &directory_object,
                       directory_raw) ||
        (directory_object.type != BORINGFS_TYPE_DIRECTORY) ||
        ((directory_object.size_bytes %
          (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE) != 0ULL)) {
        return VFS_RESULT_CORRUPT;
    }
    record_count = directory_object.size_bytes /
                   (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE;
    for (slot = 0ULL; slot < record_count; ++slot) {
        uint8_t raw[BORINGFS_DIRECTORY_RECORD_SIZE];
        struct boringfs_directory_record record;
        struct boringfs_object target;
        bool occupied = false;
        enum vfs_result result = read_directory_slot(
            boringfs, directory_id, slot, raw, &record, &target, &occupied);

        if (result != VFS_RESULT_OK) {
            return result;
        }
        if (occupied && names_equal(record.name, (size_t)record.name_length,
                                    name, name_length)) {
            return prepare_node(boringfs, record.object_id,
                                directory, node_out);
        }
    }
    return VFS_RESULT_NOT_FOUND;
}

static enum vfs_result boringfs_read(struct vfs_filesystem *filesystem,
                                     struct vfs_node *node,
                                     uint64_t offset,
                                     void *buffer,
                                     size_t length,
                                     size_t *transferred_out) {
    struct boringfs_vfs *const boringfs = context_from_filesystem(filesystem);
    uint32_t object_id;
    struct boringfs_object object;
    uint8_t raw[BORINGFS_OBJECT_RECORD_SIZE];
    uint64_t available;
    size_t transfer;

    if ((boringfs == NULL) || (buffer == NULL) ||
        (transferred_out == NULL) ||
        !node_object_id(boringfs, node, &object_id)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    *transferred_out = 0U;
    if (node->type != VFS_NODE_REGULAR) {
        return VFS_RESULT_NOT_REGULAR;
    }
    if (!decode_object(boringfs, object_id, &object, raw) ||
        (object.state != BORINGFS_OBJECT_ALLOCATED) ||
        (object.type != BORINGFS_TYPE_REGULAR) ||
        (object.object_id != object_id)) {
        return VFS_RESULT_CORRUPT;
    }
    if (offset >= object.size_bytes) {
        return VFS_RESULT_OK;
    }
    available = object.size_bytes - offset;
    transfer = length;
    if (available < (uint64_t)transfer) {
        transfer = (size_t)available;
    }
    if ((transfer != 0U) &&
        !object_read_exact(boringfs, &object, offset, buffer, transfer)) {
        return VFS_RESULT_CORRUPT;
    }
    *transferred_out = transfer;
    return VFS_RESULT_OK;
}

static enum vfs_result boringfs_readdir(struct vfs_filesystem *filesystem,
                                        struct vfs_node *directory,
                                        uint64_t index,
                                        struct vfs_dirent *entry_out) {
    struct boringfs_vfs *const boringfs = context_from_filesystem(filesystem);
    uint32_t directory_id;
    struct boringfs_object directory_object;
    uint8_t directory_raw[BORINGFS_OBJECT_RECORD_SIZE];
    uint64_t record_count;
    uint64_t slot;
    uint64_t visible_index = 0ULL;

    if ((boringfs == NULL) || (entry_out == NULL) ||
        !node_object_id(boringfs, directory, &directory_id)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (directory->type != VFS_NODE_DIRECTORY) {
        return VFS_RESULT_NOT_DIRECTORY;
    }
    if (!decode_object(boringfs, directory_id, &directory_object,
                       directory_raw) ||
        (directory_object.type != BORINGFS_TYPE_DIRECTORY) ||
        ((directory_object.size_bytes %
          (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE) != 0ULL)) {
        return VFS_RESULT_CORRUPT;
    }
    record_count = directory_object.size_bytes /
                   (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE;
    for (slot = 0ULL; slot < record_count; ++slot) {
        uint8_t raw[BORINGFS_DIRECTORY_RECORD_SIZE];
        struct boringfs_directory_record record;
        struct boringfs_object target;
        enum vfs_node_type vfs_type;
        bool occupied = false;
        enum vfs_result result = read_directory_slot(
            boringfs, directory_id, slot, raw, &record, &target, &occupied);
        size_t name_index;

        if (result != VFS_RESULT_OK) {
            return result;
        }
        if (!occupied) {
            continue;
        }
        if (visible_index != index) {
            ++visible_index;
            continue;
        }
        if (!object_type_to_vfs(target.type, &vfs_type)) {
            return VFS_RESULT_CORRUPT;
        }
        entry_out->node_id = (uint64_t)record.object_id;
        entry_out->type = vfs_type;
        entry_out->name_length = (size_t)record.name_length;
        for (name_index = 0U; name_index < (size_t)record.name_length;
             ++name_index) {
            entry_out->name[name_index] = (char)record.name[name_index];
        }
        entry_out->name[name_index] = '\0';
        return VFS_RESULT_OK;
    }
    return VFS_RESULT_NOT_FOUND;
}

static enum vfs_result readonly_mutation_result(
    struct vfs_filesystem *filesystem) {
    return (context_from_filesystem(filesystem) != NULL) ?
        VFS_RESULT_ACCESS_DENIED : VFS_RESULT_INVALID_ARGUMENT;
}

static enum vfs_result boringfs_create(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length,
                                       struct vfs_node **node_out) {
    (void)directory;
    (void)name;
    (void)name_length;
    if (node_out != NULL) {
        *node_out = NULL;
    }
    return readonly_mutation_result(filesystem);
}

static enum vfs_result boringfs_mkdir(struct vfs_filesystem *filesystem,
                                      struct vfs_node *directory,
                                      const char *name,
                                      size_t name_length,
                                      struct vfs_node **node_out) {
    (void)directory;
    (void)name;
    (void)name_length;
    if (node_out != NULL) {
        *node_out = NULL;
    }
    return readonly_mutation_result(filesystem);
}

static enum vfs_result boringfs_unlink(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length) {
    (void)directory;
    (void)name;
    (void)name_length;
    return readonly_mutation_result(filesystem);
}

static enum vfs_result boringfs_rmdir(struct vfs_filesystem *filesystem,
                                      struct vfs_node *directory,
                                      const char *name,
                                      size_t name_length) {
    (void)directory;
    (void)name;
    (void)name_length;
    return readonly_mutation_result(filesystem);
}

static enum vfs_result boringfs_rename(struct vfs_filesystem *filesystem,
                                       struct vfs_node *old_directory,
                                       const char *old_name,
                                       size_t old_name_length,
                                       struct vfs_node *new_directory,
                                       const char *new_name,
                                       size_t new_name_length) {
    (void)old_directory;
    (void)old_name;
    (void)old_name_length;
    (void)new_directory;
    (void)new_name;
    (void)new_name_length;
    return readonly_mutation_result(filesystem);
}

static enum vfs_result boringfs_write(struct vfs_filesystem *filesystem,
                                      struct vfs_node *node,
                                      uint64_t offset,
                                      const void *buffer,
                                      size_t length,
                                      size_t *transferred_out) {
    (void)node;
    (void)offset;
    (void)buffer;
    (void)length;
    if (transferred_out != NULL) {
        *transferred_out = 0U;
    }
    return readonly_mutation_result(filesystem);
}

static enum vfs_result boringfs_truncate(struct vfs_filesystem *filesystem,
                                         struct vfs_node *node,
                                         uint64_t size) {
    (void)node;
    (void)size;
    return readonly_mutation_result(filesystem);
}

static void free_mount_context(struct boringfs_vfs *boringfs,
                               uint32_t *block_owner,
                               uint8_t *reference_count) {
    if (reference_count != NULL) {
        (void)kfree(reference_count);
    }
    if (block_owner != NULL) {
        (void)kfree(block_owner);
    }
    if (boringfs != NULL) {
        if (boringfs->nodes != NULL) {
            (void)kfree(boringfs->nodes);
        }
        (void)kfree(boringfs);
    }
}

enum vfs_result boringfs_vfs_create_readonly(
    const struct block_device *device,
    uint64_t filesystem_id,
    struct boringfs_vfs **boringfs_out,
    struct boringfs_validation_error *validation_error_out) {
    struct boringfs_vfs *boringfs = NULL;
    uint8_t superblock_bytes[BORINGFS_SUPERBLOCK_HEADER_SIZE];
    struct boringfs_superblock superblock;
    uint64_t device_bytes;
    uint64_t first_sector;
    uint64_t sector_count;
    uint32_t *block_owner = NULL;
    uint8_t *reference_count = NULL;
    struct boringfs_validation_workspace workspace;
    enum boringfs_validation_result validation_result;
    struct vfs_node *root = NULL;
    size_t nodes_size;

    if (boringfs_out != NULL) {
        *boringfs_out = NULL;
    }
    if ((device == NULL) || (boringfs_out == NULL) ||
        (validation_error_out == NULL) ||
        (device->logical_block_size != BORINGFS_DEVICE_SECTOR_SIZE) ||
        (device->block_count == 0ULL) ||
        !checked_mul_u64(device->block_count,
                         (uint64_t)device->logical_block_size,
                         &device_bytes)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    validation_error_out->code = BORINGFS_VALIDATE_OK;
    validation_error_out->object_id = BORINGFS_LOCATION_NONE_U32;
    validation_error_out->block = BORINGFS_LOCATION_NONE_U32;
    validation_error_out->directory_record_index = BORINGFS_LOCATION_NONE_U64;

    boringfs = (struct boringfs_vfs *)kmalloc(sizeof(*boringfs));
    if (boringfs == NULL) {
        return VFS_RESULT_NO_SPACE;
    }
    zero_bytes((uint8_t *)boringfs, sizeof(*boringfs));
    boringfs->device = device;
    boringfs->source.context = boringfs;
    boringfs->source.size = device_bytes;
    boringfs->source.read = boringfs_source_read;
    boringfs->cached_block = BORINGFS_CACHE_INVALID_BLOCK;

    /* The source cache needs a bounded provisional geometry for block 0. */
    boringfs->superblock.total_blocks = (device->block_count /
        (uint64_t)BORINGFS_SECTORS_PER_BLOCK > (uint64_t)UINT32_MAX) ?
        UINT32_MAX :
        (uint32_t)(device->block_count /
                   (uint64_t)BORINGFS_SECTORS_PER_BLOCK);
    if (!boringfs_source_read(boringfs, 0ULL, superblock_bytes,
                              sizeof(superblock_bytes)) ||
        !boringfs_decode_superblock(superblock_bytes,
                                    sizeof(superblock_bytes), &superblock) ||
        !map_fs_blocks_to_sectors(0U, superblock.total_blocks,
                                  &first_sector, &sector_count) ||
        (first_sector != 0ULL) || (sector_count > device->block_count) ||
        (superblock.total_blocks == 0U) ||
        (superblock.total_blocks > BORINGFS_MAX_BLOCKS) ||
        (superblock.object_count < BORINGFS_MIN_OBJECTS) ||
        (superblock.object_count > BORINGFS_MAX_OBJECTS)) {
        free_mount_context(boringfs, NULL, NULL);
        validation_error_out->code = BORINGFS_VALIDATE_BAD_LAYOUT;
        return VFS_RESULT_CORRUPT;
    }
    boringfs->superblock = superblock;
    boringfs->source.size =
        (uint64_t)superblock.total_blocks * (uint64_t)BORINGFS_BLOCK_SIZE;
    boringfs->cache_valid = false;

    block_owner = (uint32_t *)kmalloc(
        (size_t)superblock.total_blocks * sizeof(uint32_t));
    reference_count = (uint8_t *)kmalloc((size_t)superblock.object_count);
    if ((block_owner == NULL) || (reference_count == NULL)) {
        free_mount_context(boringfs, block_owner, reference_count);
        return VFS_RESULT_NO_SPACE;
    }
    workspace.block_owner = block_owner;
    workspace.block_owner_count = (size_t)superblock.total_blocks;
    workspace.object_reference_count = reference_count;
    workspace.object_reference_count_count = (size_t)superblock.object_count;
    validation_result = boringfs_validate_source(
        &boringfs->source, &workspace, validation_error_out);
    if (validation_result != BORINGFS_VALIDATE_OK) {
        free_mount_context(boringfs, block_owner, reference_count);
        return VFS_RESULT_CORRUPT;
    }
    (void)kfree(reference_count);
    reference_count = NULL;
    (void)kfree(block_owner);
    block_owner = NULL;

    nodes_size = (size_t)superblock.object_count *
                 sizeof(struct boringfs_cached_node);
    boringfs->nodes = (struct boringfs_cached_node *)kmalloc(nodes_size);
    if (boringfs->nodes == NULL) {
        free_mount_context(boringfs, NULL, NULL);
        return VFS_RESULT_NO_SPACE;
    }
    zero_bytes((uint8_t *)boringfs->nodes, nodes_size);
    boringfs->node_count = superblock.object_count;

    if (!vfs_filesystem_prepare(&boringfs->filesystem, filesystem_id,
                                &boringfs_operations, boringfs) ||
        (prepare_node(boringfs, BORINGFS_ROOT_OBJECT_ID,
                      NULL, &root) != VFS_RESULT_OK) ||
        (root == NULL) || (root->type != VFS_NODE_DIRECTORY) ||
        !vfs_filesystem_set_root(&boringfs->filesystem, root)) {
        free_mount_context(boringfs, NULL, NULL);
        return VFS_RESULT_CORRUPT;
    }
    *boringfs_out = boringfs;
    return VFS_RESULT_OK;
}

struct vfs_filesystem *boringfs_vfs_get_vfs(struct boringfs_vfs *boringfs) {
    if ((boringfs == NULL) || !boringfs->filesystem.valid ||
        (boringfs->filesystem.backend_context != boringfs) ||
        (boringfs->filesystem.operations != &boringfs_operations)) {
        return NULL;
    }
    return &boringfs->filesystem;
}
