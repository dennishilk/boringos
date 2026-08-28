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
#define BORING_SYS_EVENT_WAIT 41
#define BORING_SYS_PTY_CREATE 42
#define BORING_SYS_SPAWN 43

#define BORING_SYSCALL_DEBUG_WRITE_MAX 64
#define BORING_SYSCALL_CONSOLE_IO_MAX 64
#define BORING_SYSCALL_LAUNCH_NAME_MAX 32
#define BORING_SYSCALL_EXEC_PATH_MAX 1024
#define BORING_SYSCALL_ARG_MAX 16
#define BORING_SYSCALL_ARG_BYTES_MAX 1024
#define BORING_SYSCALL_FS_IO_MAX 4096
#define BORING_SYSCALL_FD_IO_MAX 4096
#define BORING_SYSCALL_CWD_MAX 1024
#define BORING_SPAWN_FLAG_DETACHED (1U << 0)
#define BORING_SPAWN_FLAG_MASK BORING_SPAWN_FLAG_DETACHED

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
#define BORING_SYSTEM_CPU_VENDOR_CAPACITY 16U
#define BORING_SYSTEM_CPU_BRAND_CAPACITY 64U
#define BORING_SYSTEM_PLATFORM_CAPACITY 64U
#define BORING_SYSTEM_STORAGE_NAME_CAPACITY 32U
#define BORING_SYSTEM_PCI_SAMPLE_MAX 8U

#define BORING_SYSTEM_HW_CPU (1ULL << 0)
#define BORING_SYSTEM_HW_SYSTEM (1ULL << 1)
#define BORING_SYSTEM_HW_BOARD (1ULL << 2)
#define BORING_SYSTEM_HW_FIRMWARE (1ULL << 3)
#define BORING_SYSTEM_HW_SMBIOS_MEMORY (1ULL << 4)
#define BORING_SYSTEM_HW_SMBIOS_MEMORY_COMPLETE (1ULL << 5)
#define BORING_SYSTEM_HW_PCI (1ULL << 6)
#define BORING_SYSTEM_HW_PCI_COMPLETE (1ULL << 7)
#define BORING_SYSTEM_HW_FRAMEBUFFER (1ULL << 8)
#define BORING_SYSTEM_HW_STORAGE (1ULL << 9)
#define BORING_SYSTEM_HW_STORAGE_PCI (1ULL << 10)

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

struct boring_pty_create_result {
    uint32_t master_fd;
    uint32_t slave_fd;
};

struct boring_spawn_stdio {
    uint32_t stdin_fd;
    uint32_t stdout_fd;
    uint32_t stderr_fd;
    uint32_t flags;
};

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

#define BORING_SYSTEM_INFO_ABI_VERSION 3U

struct boring_system_pci_sample {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t reserved;
};

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
    uint64_t hardware_flags;
    uint32_t cpu_family;
    uint32_t cpu_model;
    uint32_t cpu_stepping;
    char cpu_vendor[BORING_SYSTEM_CPU_VENDOR_CAPACITY];
    char cpu_brand[BORING_SYSTEM_CPU_BRAND_CAPACITY];
    char system_manufacturer[BORING_SYSTEM_PLATFORM_CAPACITY];
    char system_product[BORING_SYSTEM_PLATFORM_CAPACITY];
    char board_manufacturer[BORING_SYSTEM_PLATFORM_CAPACITY];
    char board_product[BORING_SYSTEM_PLATFORM_CAPACITY];
    char firmware_vendor[BORING_SYSTEM_PLATFORM_CAPACITY];
    char firmware_version[BORING_SYSTEM_PLATFORM_CAPACITY];
    uint64_t smbios_memory_bytes;
    uint32_t smbios_memory_slots;
    uint32_t smbios_memory_devices_present;
    uint32_t pci_device_count;
    uint32_t pci_sample_count;
    struct boring_system_pci_sample pci_samples[BORING_SYSTEM_PCI_SAMPLE_MAX];
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint16_t framebuffer_bpp;
    uint16_t storage_pci_vendor_id;
    uint16_t storage_pci_device_id;
    uint8_t storage_pci_bus;
    uint8_t storage_pci_device;
    uint8_t storage_pci_function;
    uint8_t storage_read_only;
    uint32_t storage_logical_block_size;
    uint64_t storage_bytes;
    char storage_name[BORING_SYSTEM_STORAGE_NAME_CAPACITY];
    uint8_t reserved_hardware[88];
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
_Static_assert(BORING_SYS_EVENT_WAIT == 41,
               "EVENT_WAIT syscall number contract changed");
_Static_assert(BORING_SYS_PTY_CREATE == 42,
               "PTY_CREATE syscall number contract changed");
_Static_assert(BORING_SYS_SPAWN == 43,
               "SPAWN syscall number contract changed");
_Static_assert(sizeof(struct boring_pty_create_result) == 8U,
               "M36 PTY create ABI size must remain fixed");
_Static_assert(sizeof(struct boring_spawn_stdio) == 16U,
               "M36 spawn stdio ABI size must remain fixed");
_Static_assert(sizeof(struct boring_ipc_receive_result) == 16U,
               "M33 IPC receive ABI size must remain fixed");
_Static_assert(sizeof(struct boring_system_pci_sample) == 12U,
               "BoringOS PCI sample ABI size must remain fixed");
_Static_assert(sizeof(struct boring_system_info) == 1024U,
               "BoringOS system-info ABI size must remain fixed");
_Static_assert(sizeof(struct boring_process_info) == 56U,
               "BoringOS process snapshot ABI size must remain fixed");
_Static_assert(sizeof(struct boring_dirent) == 272U,
               "BoringOS dirent ABI size must remain fixed");

#endif

#endif
