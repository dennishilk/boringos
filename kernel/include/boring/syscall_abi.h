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

#define BORING_SYSCALL_DEBUG_WRITE_MAX 64
#define BORING_SYSCALL_CONSOLE_IO_MAX 64

#define BORING_SYSCALL_ENOSYS 1
#define BORING_SYSCALL_EFAULT 2
#define BORING_SYSCALL_EINVAL 3

#endif
