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

#define BORING_SYSCALL_DEBUG_WRITE_MAX 64
#define BORING_SYSCALL_CONSOLE_IO_MAX 64
#define BORING_SYSCALL_LAUNCH_NAME_MAX 32
#define BORING_SYSCALL_FS_IO_MAX 4096

#define BORING_DIRENT_TYPE_DIRECTORY 1U
#define BORING_DIRENT_TYPE_REGULAR 2U
#define BORING_DIRENT_NAME_CAPACITY 256U

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

#ifndef __ASSEMBLER__

#include <stdint.h>

struct boring_dirent {
    uint64_t node_id;
    uint32_t type;
    uint32_t name_length;
    char name[BORING_DIRENT_NAME_CAPACITY];
};

_Static_assert(sizeof(struct boring_dirent) == 272U,
               "BoringOS dirent ABI size must remain fixed");

#endif

#endif
