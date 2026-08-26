#include <stddef.h>
#include <stdint.h>

#include <boring/syscall.h>
#include <boring/syscall_abi.h>

uint64_t boring_getpid(void) {
    uint64_t result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_GETPID)
        : "rcx", "r11", "cc");

    return result;
}

long boring_debug_write(const void *buffer, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_DEBUG_WRITE), "D"(buffer), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_console_write(const void *buffer, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_CONSOLE_WRITE), "D"(buffer), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_console_read(void *buffer, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_CONSOLE_READ), "D"(buffer), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_launch_argv(const char *path,
                        size_t path_length,
                        const char *const argv[],
                        size_t argc) {
    long result;
    register size_t argc_argument __asm__("r10") = argc;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_LAUNCH), "D"(path), "S"(path_length),
          "d"(argv), "r"(argc_argument)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_launch(const char *program_name, size_t length) {
    const char *argv[1];

    argv[0] = program_name;
    return boring_launch_argv(program_name, length, argv, 1U);
}

long boring_fs_readdir(const char *path,
                       size_t path_length,
                       uint64_t index,
                       struct boring_dirent *entry) {
    long result;
    register struct boring_dirent *entry_argument __asm__("r10") = entry;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_READDIR), "D"(path),
          "S"(path_length), "d"(index), "r"(entry_argument)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fs_mkdir(const char *name, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_MKDIR), "D"(name), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fs_rmdir(const char *name, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_RMDIR), "D"(name), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fs_chdir(const char *path, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_CHDIR), "D"(path), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fs_read(const char *path,
                    size_t path_length,
                    uint64_t offset,
                    void *buffer,
                    size_t capacity) {
    long result;
    register void *buffer_argument __asm__("r10") = buffer;
    register size_t capacity_argument __asm__("r8") = capacity;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_READ), "D"(path), "S"(path_length),
          "d"(offset), "r"(buffer_argument), "r"(capacity_argument)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fs_touch(const char *path, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_TOUCH), "D"(path), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fs_write(const char *path,
                     size_t path_length,
                     const void *buffer,
                     size_t length) {
    long result;
    register size_t length_argument __asm__("r10") = length;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_WRITE), "D"(path), "S"(path_length),
          "d"(buffer), "r"(length_argument)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fs_unlink(const char *path, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FS_UNLINK), "D"(path), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_system_info(struct boring_system_info *info) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_INFO), "D"(info)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_getcwd(char *buffer, size_t capacity) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_GETCWD), "D"(buffer), "S"(capacity)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_process_snapshot(uint64_t index, struct boring_process_info *info) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_PROCESS_SNAPSHOT), "D"(index), "S"(info)
        : "rcx", "r11", "cc", "memory");
    return result;
}

void boring_exit(int status) {
    __asm__ volatile(
        "syscall"
        :
        : "a"((uint64_t)BORING_SYS_EXIT), "D"((int64_t)status)
        : "rcx", "r11", "cc", "memory");
    for (;;) {
        __asm__ volatile ("pause");
    }
}

long boring_waitpid(uint64_t pid, int *status) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_WAITPID), "D"(pid), "S"(status)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fd_open(const char *path, size_t path_length, uint32_t flags) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FD_OPEN), "D"(path), "S"(path_length),
          "d"((uint64_t)flags)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fd_read(uint32_t fd, void *buffer, size_t capacity) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FD_READ), "D"((uint64_t)fd),
          "S"(buffer), "d"(capacity)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fd_write(uint32_t fd, const void *buffer, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FD_WRITE), "D"((uint64_t)fd),
          "S"(buffer), "d"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_fd_close(uint32_t fd) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FD_CLOSE), "D"((uint64_t)fd)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_input_claim(void) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_INPUT_CLAIM)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_input_read(struct boring_input_event *events, size_t max_events) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_INPUT_READ), "D"(events), "S"(max_events)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_input_release(void) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_INPUT_RELEASE)
        : "rcx", "r11", "cc", "memory");
    return result;
}


long boring_memory_alloc_raw(size_t size) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_MEMORY_ALLOC), "D"(size)
        : "rcx", "r11", "cc", "memory");
    return result;
}

void *boring_memory_alloc(size_t size) {
    const long result = boring_memory_alloc_raw(size);

    return (result > 0L) ? (void *)(uintptr_t)result : NULL;
}

long boring_memory_free(void *base) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_MEMORY_FREE), "D"(base)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_buffer_create(size_t size) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_CREATE), "D"(size)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_buffer_map_raw(uint32_t handle) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_MAP), "D"((uint64_t)handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}

void *boring_buffer_map(uint32_t handle) {
    const long result = boring_buffer_map_raw(handle);

    return (result > 0L) ? (void *)(uintptr_t)result : NULL;
}

long boring_buffer_unmap(void *base) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_UNMAP), "D"(base)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_buffer_close(uint32_t handle) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_CLOSE), "D"((uint64_t)handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}
