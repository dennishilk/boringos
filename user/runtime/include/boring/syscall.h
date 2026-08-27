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
long boring_launch_argv(const char *path, size_t path_length,
                        const char *const argv[], size_t argc);
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
long boring_system_info(struct boring_system_info *info);
long boring_getcwd(char *buffer, size_t capacity);
long boring_process_snapshot(uint64_t index, struct boring_process_info *info);
void boring_exit(int status) __attribute__((noreturn));
long boring_waitpid(uint64_t pid, int *status);
long boring_fd_open(const char *path, size_t path_length, uint32_t flags);
long boring_fd_read(uint32_t fd, void *buffer, size_t capacity);
long boring_fd_write(uint32_t fd, const void *buffer, size_t length);
long boring_fd_close(uint32_t fd);
long boring_pty_create(struct boring_pty_create_result *result);
long boring_spawn(const char *path, size_t path_length,
                  const char *const argv[], size_t argc,
                  const struct boring_spawn_stdio *stdio_config);
long boring_input_claim(void);
long boring_input_read(struct boring_input_event *events, size_t max_events);
long boring_input_release(void);

/* Raw forms expose exact negative BoringOS errno values for acceptance tests. */
long boring_memory_alloc_raw(size_t size);
void *boring_memory_alloc(size_t size);
long boring_memory_free(void *base);
long boring_buffer_create(size_t size);
long boring_buffer_map_raw(uint32_t handle);
void *boring_buffer_map(uint32_t handle);
long boring_buffer_unmap(void *base);
long boring_buffer_close(uint32_t handle);

#endif
