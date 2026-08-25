#ifndef BORING_BORINGFS_VFS_H
#define BORING_BORINGFS_VFS_H

#include <stdint.h>

#include <boring/block_device.h>
#include <boring/boringfs.h>
#include <boring/vfs.h>

struct boringfs_vfs;

enum vfs_result boringfs_vfs_create_readonly(
    const struct block_device *device,
    uint64_t filesystem_id,
    struct boringfs_vfs **boringfs_out,
    struct boringfs_validation_error *validation_error_out);
struct vfs_filesystem *boringfs_vfs_get_vfs(struct boringfs_vfs *boringfs);

#endif
