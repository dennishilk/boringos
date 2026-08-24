#ifndef BORING_RAMFS_H
#define BORING_RAMFS_H

#include <stdint.h>

#include <boring/vfs.h>

#define RAMFS_MAX_FILESYSTEMS 8U
#define RAMFS_MAX_NODES 32U
#define RAMFS_FILE_MAX 8192U
#define RAMFS_TOTAL_DATA_MAX 32768U

struct ramfs;

struct ramfs_stats {
    uint64_t live_nodes;
    uint64_t live_directories;
    uint64_t live_files;
    uint64_t live_file_bytes;
    uint64_t allocated_file_capacity;
};

enum vfs_result ramfs_create_filesystem(uint64_t filesystem_id,
                                        struct ramfs **ramfs_out);
struct vfs_filesystem *ramfs_get_vfs(struct ramfs *ramfs);
enum vfs_result ramfs_get_stats(const struct ramfs *ramfs,
                                struct ramfs_stats *stats_out);
enum vfs_result ramfs_destroy_filesystem(struct ramfs *ramfs);

#endif
