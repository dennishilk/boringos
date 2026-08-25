#ifndef BORING_SYSCALL_ABI_H
#define BORING_SYSCALL_ABI_H

/*
 * Provisional BoringOS syscall ABI v0 constants shared by the kernel and the
 * deliberately tiny native userspace programs.
 *
 * This header does not promise ABI stability.
 */
#define BORING_SYS_GETPID 0
#define BORING_SYS_DEBUG_WRITE 1
#define BORING_SYS_CONSOLE_WRITE 2
#define BORING_SYS_CONSOLE_READ 3
#define BORING_SYS_LAUNCH 4
#define BORING_SYS_FS_READDIR 5
#define BORING_SYS_FS_MKDIR 6
#define BORING_SYS_FS_RMDIR 7
#define BORING_SYS_FS_CHDIR 8
#define BORING_SYS_FS_READ 9
#define BORING_SYS_FS_TOUCH 10
#define BORING_SYS_FS_WRITE 11
#define BORING_SYS_FS_UNLINK 12
#define BORING_SYS_INFO 13
#define BORING_SYS_GETCWD 14
#define BORING_SYS_PROCESS_SNAPSHOT 15
#define BORING_SYS_EXIT 16
#define BORING_SYS_WAITPID 17

#define BORING_SYSCALL_DEBUG_WRITE_MAX 64
#define BORING_SYSCALL_CONSOLE_IO_MAX 64
#define BORING_SYSCALL_LAUNCH_NAME_MAX 32
#define BORING_SYSCALL_FS_IO_MAX 4096
#define BORING_SYSCALL_CWD_MAX 1024

#define BORING_DIRENT_TYPE_DIRECTORY 1U
#define BORING_DIRENT_TYPE_REGULAR 2U
#define BORING_DIRENT_NAME_CAPACITY 256U

#define BORING_PROCESS_STATE_RUNNING 1U
#define BORING_PROCESS_STATE_WAITING 2U
#define BORING_PROCESS_STATE_ZOMBIE 3U
#define BORING_PROCESS_NAME_CAPACITY 32U

#define BORING_SYSTEM_HOSTNAME_CAPACITY 32U
#define BORING_SYSTEM_USERNAME_CAPACITY 32U
#define BORING_SYSTEM_OS_CAPACITY 16U
#define BORING_SYSTEM_KERNEL_CAPACITY 32U
#define BORING_SYSTEM_VERSION_CAPACITY 32U
#define BORING_SYSTEM_ARCH_CAPACITY 16U
#define BORING_SYSTEM_FS_CAPACITY 16U
#define BORING_SYSTEM_DEVICE_CAPACITY 32U

#define BORING_SYSCALL_ENOSYS 1
#define BORING_SYSCALL_EFAULT 2
#define BORING_SYSCALL_EINVAL 3
#define BORING_SYSCALL_ENOENT 4
#define BORING_SYSCALL_EEXIST 5
#define BORING_SYSCALL_ENOTDIR 6
#define BORING_SYSCALL_ENOTEMPTY 7
#define BORING_SYSCALL_ENOSPC 8
#define BORING_SYSCALL_EBUSY 9
#define BORING_SYSCALL_ENAMETOOLONG 10
#define BORING_SYSCALL_EACCES 11
#define BORING_SYSCALL_ENOTSUP 12
#define BORING_SYSCALL_EIO 13
#define BORING_SYSCALL_EISDIR 14

#ifndef __ASSEMBLER__

#include <stdint.h>

struct boring_dirent {
    uint64_t node_id;
    uint32_t type;
    uint32_t name_length;
    char name[BORING_DIRENT_NAME_CAPACITY];
};

struct boring_process_info {
    uint64_t pid;
    uint64_t parent_pid;
    uint32_t state;
    uint32_t reserved;
    char name[BORING_PROCESS_NAME_CAPACITY];
};

#define BORING_SYSTEM_INFO_ABI_VERSION 2U

struct boring_system_info {
    uint32_t abi_version;
    uint32_t reserved;
    uint64_t usable_memory_bytes;
    uint64_t free_memory_bytes;
    uint64_t uptime_ticks;
    uint32_t timer_frequency_millihz;
    uint32_t process_count;
    uint64_t current_pid;
    char hostname[BORING_SYSTEM_HOSTNAME_CAPACITY];
    char username[BORING_SYSTEM_USERNAME_CAPACITY];
    char os_name[BORING_SYSTEM_OS_CAPACITY];
    char kernel_name[BORING_SYSTEM_KERNEL_CAPACITY];
    char kernel_version[BORING_SYSTEM_VERSION_CAPACITY];
    char arch[BORING_SYSTEM_ARCH_CAPACITY];
    char root_fs[BORING_SYSTEM_FS_CAPACITY];
    char root_device[BORING_SYSTEM_DEVICE_CAPACITY];
};

_Static_assert(sizeof(struct boring_system_info) == 256U,
               "BoringOS system-info ABI size must remain fixed");
_Static_assert(sizeof(struct boring_process_info) == 56U,
               "BoringOS process snapshot ABI size must remain fixed");
_Static_assert(sizeof(struct boring_dirent) == 272U,
               "BoringOS dirent ABI size must remain fixed");

#endif

#endif
