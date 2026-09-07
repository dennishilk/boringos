#ifndef BORING_ELF_VFS_H
#define BORING_ELF_VFS_H

#include <stdbool.h>

#include <boring/elf_loader.h>
#include <boring/vfs.h>

struct process;

struct boring_elf_vfs_source {
    struct boring_elf_source source;
    struct vfs_handle handle;
    bool open;
};

enum vfs_result boring_elf_vfs_source_open(
    const struct process *process,
    const char *path,
    struct boring_elf_vfs_source *source_out);
enum vfs_result boring_elf_vfs_source_close(
    struct boring_elf_vfs_source *source);

#endif
