#ifndef BORING_USER_SYSCALL_H
#define BORING_USER_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

#include <boring/syscall_abi.h>

uint64_t boring_getpid(void);
long boring_debug_write(const void *buffer, size_t length);
long boring_console_write(const void *buffer, size_t length);
long boring_console_read(void *buffer, size_t length);
long boring_launch(const char *program_name, size_t length);
long boring_fs_readdir(const char *path,
                       size_t path_length,
                       uint64_t index,
                       struct boring_dirent *entry);
long boring_fs_mkdir(const char *name, size_t length);
long boring_fs_rmdir(const char *name, size_t length);
long boring_fs_chdir(const char *path, size_t length);
long boring_fs_read(const char *path,
                    size_t path_length,
                    uint64_t offset,
                    void *buffer,
                    size_t capacity);
long boring_fs_touch(const char *path, size_t length);
long boring_fs_write(const char *path,
                     size_t path_length,
                     const void *buffer,
                     size_t length);
long boring_fs_unlink(const char *path, size_t length);

#endif
