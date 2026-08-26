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
#define BORING_SYS_FD_OPEN 18
#define BORING_SYS_FD_READ 19
#define BORING_SYS_FD_WRITE 20
#define BORING_SYS_FD_CLOSE 21
#define BORING_SYS_INPUT_CLAIM 22
#define BORING_SYS_INPUT_READ 23
#define BORING_SYS_INPUT_RELEASE 24
#define BORING_SYS_MEMORY_ALLOC 25
#define BORING_SYS_MEMORY_FREE 26
#define BORING_SYS_BUFFER_CREATE 27
#define BORING_SYS_BUFFER_MAP 28
#define BORING_SYS_BUFFER_UNMAP 29
#define BORING_SYS_BUFFER_CLOSE 30
#define BORING_SYS_SERVICE_REGISTER 31
#define BORING_SYS_SERVICE_CONNECT 32
#define BORING_SYS_SERVICE_ACCEPT 33
#define BORING_SYS_IPC_SEND 34
#define BORING_SYS_IPC_RECEIVE 35
#define BORING_SYS_IPC_CLOSE 36
#define BORING_SYS_BUFFER_INFO 37
#define BORING_SYS_FRAMEBUFFER_CLAIM 38
#define BORING_SYS_FRAMEBUFFER_PRESENT 39
#define BORING_SYS_FRAMEBUFFER_RELEASE 40

#define BORING_SYSCALL_DEBUG_WRITE_MAX 64
#define BORING_SYSCALL_CONSOLE_IO_MAX 64
#define BORING_SYSCALL_LAUNCH_NAME_MAX 32
#define BORING_SYSCALL_EXEC_PATH_MAX 1024
#define BORING_SYSCALL_ARG_MAX 16
#define BORING_SYSCALL_ARG_BYTES_MAX 1024
#define BORING_SYSCALL_FS_IO_MAX 4096
#define BORING_SYSCALL_FD_IO_MAX 4096
#define BORING_SYSCALL_CWD_MAX 1024

#define BORING_MEMORY_PAGE_SIZE 4096ULL
#define BORING_MEMORY_ALLOC_MAX_BYTES (16ULL * 1024ULL * 1024ULL)
#define BORING_BUFFER_MAX_BYTES (64ULL * 1024ULL * 1024ULL)
#define BORING_MEMORY_ALLOCATION_MAX 32U
#define BORING_BUFFER_HANDLE_MAX 32U
#define BORING_BUFFER_MAPPING_MAX 32U
#define BORING_BUFFER_OBJECT_MAX 64U
#define BORING_BUFFER_HANDLE_INVALID 0U

#define BORING_IPC_SERVICE_NAME_MAX 31U
#define BORING_IPC_HANDLE_INVALID 0U
#define BORING_IPC_NO_ATTACHED_BUFFER BORING_BUFFER_HANDLE_INVALID
#define BORING_IPC_INLINE_PAYLOAD_MAX 256U
#define BORING_IPC_RECEIVE_FLAGS_NONE 0U

#define BORING_FD_STDIN 0U
#define BORING_FD_STDOUT 1U
#define BORING_FD_STDERR 2U
#define BORING_FD_OPEN_READ (1U << 0)
#define BORING_FD_OPEN_WRITE (1U << 1)
#define BORING_FD_OPEN_MASK (BORING_FD_OPEN_READ | BORING_FD_OPEN_WRITE)

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
#define BORING_SYSCALL_ENOEXEC 15
#define BORING_SYSCALL_ENOMEM 16
#define BORING_SYSCALL_EPIPE 17

#ifndef __ASSEMBLER__

#include <stdint.h>

#include <boring/input_abi.h>

struct boring_ipc_receive_result {
    uint64_t payload_length;
    uint32_t buffer_handle;
    uint32_t flags;
};

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

_Static_assert(BORING_SYS_GETPID == 0,
               "GETPID syscall number contract changed");
_Static_assert(BORING_SYS_DEBUG_WRITE == 1,
               "DEBUG_WRITE syscall number contract changed");
_Static_assert(BORING_SYS_CONSOLE_WRITE == 2,
               "CONSOLE_WRITE syscall number contract changed");
_Static_assert(BORING_SYS_CONSOLE_READ == 3,
               "CONSOLE_READ syscall number contract changed");
_Static_assert(BORING_SYS_LAUNCH == 4,
               "LAUNCH syscall number contract changed");
_Static_assert(BORING_SYS_FS_READDIR == 5,
               "FS_READDIR syscall number contract changed");
_Static_assert(BORING_SYS_FS_MKDIR == 6,
               "FS_MKDIR syscall number contract changed");
_Static_assert(BORING_SYS_FS_RMDIR == 7,
               "FS_RMDIR syscall number contract changed");
_Static_assert(BORING_SYS_FS_CHDIR == 8,
               "FS_CHDIR syscall number contract changed");
_Static_assert(BORING_SYS_FS_READ == 9,
               "FS_READ syscall number contract changed");
_Static_assert(BORING_SYS_FS_TOUCH == 10,
               "FS_TOUCH syscall number contract changed");
_Static_assert(BORING_SYS_FS_WRITE == 11,
               "FS_WRITE syscall number contract changed");
_Static_assert(BORING_SYS_FS_UNLINK == 12,
               "FS_UNLINK syscall number contract changed");
_Static_assert(BORING_SYS_INFO == 13,
               "INFO syscall number contract changed");
_Static_assert(BORING_SYS_GETCWD == 14,
               "GETCWD syscall number contract changed");
_Static_assert(BORING_SYS_PROCESS_SNAPSHOT == 15,
               "PROCESS_SNAPSHOT syscall number contract changed");
_Static_assert(BORING_SYS_EXIT == 16,
               "EXIT syscall number contract changed");
_Static_assert(BORING_SYS_WAITPID == 17,
               "WAITPID syscall number contract changed");
_Static_assert(BORING_SYS_FD_OPEN == 18,
               "FD_OPEN syscall number contract changed");
_Static_assert(BORING_SYS_FD_READ == 19,
               "FD_READ syscall number contract changed");
_Static_assert(BORING_SYS_FD_WRITE == 20,
               "FD_WRITE syscall number contract changed");
_Static_assert(BORING_SYS_FD_CLOSE == 21,
               "FD_CLOSE syscall number contract changed");
_Static_assert(BORING_SYS_INPUT_CLAIM == 22,
               "INPUT_CLAIM syscall number contract changed");
_Static_assert(BORING_SYS_INPUT_READ == 23,
               "INPUT_READ syscall number contract changed");
_Static_assert(BORING_SYS_INPUT_RELEASE == 24,
               "INPUT_RELEASE syscall number contract changed");
_Static_assert(BORING_SYS_MEMORY_ALLOC == 25,
               "MEMORY_ALLOC syscall number contract changed");
_Static_assert(BORING_SYS_MEMORY_FREE == 26,
               "MEMORY_FREE syscall number contract changed");
_Static_assert(BORING_SYS_BUFFER_CREATE == 27,
               "BUFFER_CREATE syscall number contract changed");
_Static_assert(BORING_SYS_BUFFER_MAP == 28,
               "BUFFER_MAP syscall number contract changed");
_Static_assert(BORING_SYS_BUFFER_UNMAP == 29,
               "BUFFER_UNMAP syscall number contract changed");
_Static_assert(BORING_SYS_BUFFER_CLOSE == 30,
               "BUFFER_CLOSE syscall number contract changed");
_Static_assert(BORING_SYS_SERVICE_REGISTER == 31,
               "SERVICE_REGISTER syscall number contract changed");
_Static_assert(BORING_SYS_SERVICE_CONNECT == 32,
               "SERVICE_CONNECT syscall number contract changed");
_Static_assert(BORING_SYS_SERVICE_ACCEPT == 33,
               "SERVICE_ACCEPT syscall number contract changed");
_Static_assert(BORING_SYS_IPC_SEND == 34,
               "IPC_SEND syscall number contract changed");
_Static_assert(BORING_SYS_IPC_RECEIVE == 35,
               "IPC_RECEIVE syscall number contract changed");
_Static_assert(BORING_SYS_IPC_CLOSE == 36,
               "IPC_CLOSE syscall number contract changed");
_Static_assert(BORING_SYS_BUFFER_INFO == 37,
               "BUFFER_INFO syscall number contract changed");
_Static_assert(BORING_SYS_FRAMEBUFFER_CLAIM == 38,
               "FRAMEBUFFER_CLAIM syscall number contract changed");
_Static_assert(BORING_SYS_FRAMEBUFFER_PRESENT == 39,
               "FRAMEBUFFER_PRESENT syscall number contract changed");
_Static_assert(BORING_SYS_FRAMEBUFFER_RELEASE == 40,
               "FRAMEBUFFER_RELEASE syscall number contract changed");
_Static_assert(sizeof(struct boring_ipc_receive_result) == 16U,
               "M33 IPC receive ABI size must remain fixed");
_Static_assert(sizeof(struct boring_system_info) == 256U,
               "BoringOS system-info ABI size must remain fixed");
_Static_assert(sizeof(struct boring_process_info) == 56U,
               "BoringOS process snapshot ABI size must remain fixed");
_Static_assert(sizeof(struct boring_dirent) == 272U,
               "BoringOS dirent ABI size must remain fixed");

#endif

#endif
