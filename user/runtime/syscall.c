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

long boring_launch(const char *program_name, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_LAUNCH), "D"(program_name), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
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
