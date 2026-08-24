#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_loader.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/vfs.h>
#include <boring/vmm.h>

#define IA32_EFER 0xc0000080U
#define IA32_STAR 0xc0000081U
#define IA32_LSTAR 0xc0000082U
#define IA32_FMASK 0xc0000084U
#define EFER_SCE (1ULL << 0)

#define RFLAGS_CF (1ULL << 0)
#define RFLAGS_RESERVED1 (1ULL << 1)
#define RFLAGS_PF (1ULL << 2)
#define RFLAGS_AF (1ULL << 4)
#define RFLAGS_ZF (1ULL << 6)
#define RFLAGS_SF (1ULL << 7)
#define RFLAGS_TF (1ULL << 8)
#define RFLAGS_IF (1ULL << 9)
#define RFLAGS_DF (1ULL << 10)
#define RFLAGS_OF (1ULL << 11)
#define RFLAGS_IOPL (3ULL << 12)
#define RFLAGS_NT (1ULL << 14)
#define RFLAGS_VM (1ULL << 17)
#define RFLAGS_AC (1ULL << 18)
#define SYSCALL_FMASK_VALUE (RFLAGS_TF | RFLAGS_IF | RFLAGS_DF | RFLAGS_NT | RFLAGS_AC)
#define SYSRET_ALLOWED_STATUS_FLAGS (RFLAGS_CF | RFLAGS_PF | RFLAGS_AF | RFLAGS_ZF | RFLAGS_SF | RFLAGS_OF)

#define STAR_KERNEL_SELECTOR ((uint64_t)(X86_64_GDT_KERNEL_CODE_SELECTOR & 0xfffcU))
#define STAR_USER_DATA_BASE ((uint64_t)(X86_64_GDT_USER_DATA_SELECTOR & 0xfffcU))
#define STAR_SYSRET_BASE (STAR_USER_DATA_BASE - 8ULL)
#define STAR_VALUE ((STAR_SYSRET_BASE << 48U) | (STAR_KERNEL_SELECTOR << 32U))

#define BOOTSTRAP_PROGRAM_STACK_BASE 0x0000000040010000ULL
#define BOOTSTRAP_PROGRAM_STACK_SIZE ((size_t)BORING_ELF_PAGE_SIZE)

struct syscall_bootstrap_program {
    char name[BORING_SYSCALL_LAUNCH_NAME_MAX + 1U];
    size_t name_length;
    const uint8_t *module_bytes;
    size_t module_size;
    bool registered;
};

_Static_assert(sizeof(struct x86_64_syscall_frame) == 144U,
               "syscall frame must match x86_64 entry assembly");
_Static_assert(offsetof(struct x86_64_syscall_frame, user_rsp) == 0U,
               "syscall user RSP offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, user_rip) == 8U,
               "syscall user RIP offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, user_rflags) == 16U,
               "syscall user RFLAGS offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, syscall_number) == 24U,
               "syscall number offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, result) == 128U,
               "syscall result offset mismatch");
_Static_assert((X86_64_GDT_KERNEL_DATA_SELECTOR ==
                (X86_64_GDT_KERNEL_CODE_SELECTOR + 8U)),
               "SYSCALL kernel SS must follow kernel CS");
_Static_assert((((STAR_SYSRET_BASE + 8ULL) | 3ULL) ==
                (uint64_t)X86_64_GDT_USER_DATA_SELECTOR),
               "SYSRET user SS relationship mismatch");
_Static_assert((((STAR_SYSRET_BASE + 16ULL) | 3ULL) ==
                (uint64_t)X86_64_GDT_USER_CODE_SELECTOR),
               "SYSRET user CS relationship mismatch");
_Static_assert((uint32_t)VFS_NODE_DIRECTORY == BORING_DIRENT_TYPE_DIRECTORY,
               "directory type ABI mismatch");
_Static_assert((uint32_t)VFS_NODE_REGULAR == BORING_DIRENT_TYPE_REGULAR,
               "regular type ABI mismatch");
_Static_assert((VFS_NAME_MAX + 1U) == BORING_DIRENT_NAME_CAPACITY,
               "dirent name capacity must match VFS name bound");

extern void x86_64_syscall_entry(void);

uint8_t x86_64_syscall_stack[X86_64_SYSCALL_STACK_SIZE]
    __attribute__((aligned(16)));
uint64_t x86_64_syscall_user_rsp_scratch __attribute__((aligned(8)));

static struct syscall_stats syscall_state;
static struct syscall_bootstrap_program bootstrap_program;
static bool syscall_initialized;

static void syscall_fatal(const char *reason) __attribute__((noreturn));
static void syscall_fatal(const char *reason) {
    serial_write_string("BoringKernel syscall fatal: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static uint64_t syscall_error(int error_number) {
    return (uint64_t)(-(int64_t)error_number);
}

static void bootstrap_program_clear(void) {
    size_t index;

    for (index = 0U;
         index <= (size_t)BORING_SYSCALL_LAUNCH_NAME_MAX; ++index) {
        bootstrap_program.name[index] = '\0';
    }
    bootstrap_program.name_length = 0U;
    bootstrap_program.module_bytes = NULL;
    bootstrap_program.module_size = 0U;
    bootstrap_program.registered = false;
}

bool syscall_stack_contains(uintptr_t stack_pointer) {
    const uintptr_t base = (uintptr_t)&x86_64_syscall_stack[0];
    const uintptr_t top = base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;

    return (stack_pointer >= base) && (stack_pointer < top);
}

static uint64_t sanitize_user_rflags(uint64_t user_rflags) {
    return (user_rflags & SYSRET_ALLOWED_STATUS_FLAGS) | RFLAGS_RESERVED1;
}

static bool syscall_user_range_accessible(uintptr_t user_address,
                                          size_t length,
                                          bool require_writable) {
    struct process *process;
    uintptr_t current;
    size_t remaining;

    if (!ring3_user_range_valid(user_address, length)) {
        return false;
    }

    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap) {
        return false;
    }

    current = user_address;
    remaining = length;
    while (remaining != 0U) {
        uint64_t physical;
        const size_t page_offset =
            (size_t)((uint64_t)current & (VMM_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)VMM_PAGE_SIZE - page_offset;

        if (chunk > remaining) {
            chunk = remaining;
        }
        if (!ring3_user_translate(&process->address_space, current,
                                  require_writable, &physical)) {
            return false;
        }

        current += (uintptr_t)chunk;
        remaining -= chunk;
    }

    return true;
}

static bool syscall_copy_from_user(void *destination,
                                   uintptr_t user_address,
                                   size_t length) {
    struct process *process;
    struct vmm_stats vmm_stats;
    uint8_t *output = (uint8_t *)destination;
    uintptr_t current;
    size_t remaining;

    if ((destination == NULL) || !ring3_user_range_valid(user_address, length) ||
        !vmm_get_stats(&vmm_stats)) {
        return false;
    }

    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap) {
        return false;
    }

    current = user_address;
    remaining = length;
    while (remaining != 0U) {
        uint64_t physical;
        uint64_t kernel_virtual;
        const size_t page_offset =
            (size_t)((uint64_t)current & (VMM_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)VMM_PAGE_SIZE - page_offset;
        size_t index;
        const uint8_t *source;

        if (chunk > remaining) {
            chunk = remaining;
        }

        if (!ring3_user_translate(&process->address_space, current, false,
                                  &physical) ||
            (physical > UINT64_MAX - vmm_stats.hhdm_offset)) {
            return false;
        }

        kernel_virtual = vmm_stats.hhdm_offset + physical;
        if ((kernel_virtual >> 48U) != 0xffffULL) {
            return false;
        }
        source = (const uint8_t *)(uintptr_t)kernel_virtual;

        for (index = 0U; index < chunk; ++index) {
            output[index] = source[index];
        }

        output += chunk;
        current += (uintptr_t)chunk;
        remaining -= chunk;
    }

    return true;
}

static bool syscall_copy_to_user(uintptr_t user_address,
                                 const void *source_buffer,
                                 size_t length) {
    struct process *process;
    struct vmm_stats vmm_stats;
    const uint8_t *input = (const uint8_t *)source_buffer;
    uintptr_t current;
    size_t remaining;

    if ((source_buffer == NULL) ||
        !ring3_user_range_valid(user_address, length) ||
        !vmm_get_stats(&vmm_stats)) {
        return false;
    }

    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap) {
        return false;
    }

    current = user_address;
    remaining = length;
    while (remaining != 0U) {
        uint64_t physical;
        uint64_t kernel_virtual;
        const size_t page_offset =
            (size_t)((uint64_t)current & (VMM_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)VMM_PAGE_SIZE - page_offset;
        size_t index;
        uint8_t *destination;

        if (chunk > remaining) {
            chunk = remaining;
        }

        if (!ring3_user_translate(&process->address_space, current, true,
                                  &physical) ||
            (physical > UINT64_MAX - vmm_stats.hhdm_offset)) {
            return false;
        }

        kernel_virtual = vmm_stats.hhdm_offset + physical;
        if ((kernel_virtual >> 48U) != 0xffffULL) {
            return false;
        }
        destination = (uint8_t *)(uintptr_t)kernel_virtual;

        for (index = 0U; index < chunk; ++index) {
            destination[index] = input[index];
        }

        input += chunk;
        current += (uintptr_t)chunk;
        remaining -= chunk;
    }

    return true;
}

static int syscall_copy_explicit_string(uint64_t user_address,
                                        uint64_t length,
                                        size_t maximum,
                                        char *buffer) {
    size_t safe_length;
    size_t index;

    if (length == 0ULL) {
        return BORING_SYSCALL_EINVAL;
    }
    if (length > (uint64_t)maximum) {
        return BORING_SYSCALL_ENAMETOOLONG;
    }
    if ((buffer == NULL) || (length > (uint64_t)SIZE_MAX)) {
        return BORING_SYSCALL_EINVAL;
    }

    safe_length = (size_t)length;
    if (!syscall_copy_from_user(buffer, (uintptr_t)user_address,
                                safe_length)) {
        return BORING_SYSCALL_EFAULT;
    }
    for (index = 0U; index < safe_length; ++index) {
        if (buffer[index] == '\0') {
            return BORING_SYSCALL_EINVAL;
        }
    }
    buffer[safe_length] = '\0';
    return 0;
}

static int syscall_vfs_error(enum vfs_result result) {
    switch (result) {
        case VFS_RESULT_OK:
            return 0;
        case VFS_RESULT_NOT_FOUND:
            return BORING_SYSCALL_ENOENT;
        case VFS_RESULT_ALREADY_EXISTS:
            return BORING_SYSCALL_EEXIST;
        case VFS_RESULT_NOT_DIRECTORY:
        case VFS_RESULT_NOT_REGULAR:
            return BORING_SYSCALL_ENOTDIR;
        case VFS_RESULT_NOT_EMPTY:
            return BORING_SYSCALL_ENOTEMPTY;
        case VFS_RESULT_NO_SPACE:
            return BORING_SYSCALL_ENOSPC;
        case VFS_RESULT_BUSY:
        case VFS_RESULT_ALREADY_MOUNTED:
        case VFS_RESULT_MOUNT_CONFLICT:
            return BORING_SYSCALL_EBUSY;
        case VFS_RESULT_PATH_TOO_LONG:
        case VFS_RESULT_NAME_TOO_LONG:
            return BORING_SYSCALL_ENAMETOOLONG;
        case VFS_RESULT_ACCESS_DENIED:
            return BORING_SYSCALL_EACCES;
        case VFS_RESULT_NOT_SUPPORTED:
        case VFS_RESULT_CROSS_FILESYSTEM:
            return BORING_SYSCALL_ENOTSUP;
        case VFS_RESULT_INVALID_ARGUMENT:
        case VFS_RESULT_EMPTY_PATH:
        case VFS_RESULT_NO_CWD:
        case VFS_RESULT_OVERFLOW:
            return BORING_SYSCALL_EINVAL;
        case VFS_RESULT_NOT_INITIALIZED:
        case VFS_RESULT_ALREADY_INITIALIZED:
        case VFS_RESULT_CORRUPT:
        default:
            return BORING_SYSCALL_EIO;
    }
}

static bool bootstrap_program_name_equals(const char *name, size_t length) {
    size_t index;

    if (!bootstrap_program.registered || (name == NULL) ||
        (length != bootstrap_program.name_length)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (name[index] != bootstrap_program.name[index]) {
            return false;
        }
    }
    return true;
}

bool syscall_register_bootstrap_program(const char *name,
                                        size_t name_length,
                                        const uint8_t *module_bytes,
                                        size_t module_size) {
    struct boring_elf_validation validation;
    size_t index;

    if (!syscall_initialized || bootstrap_program.registered ||
        (name == NULL) || (name_length == 0U) ||
        (name_length > (size_t)BORING_SYSCALL_LAUNCH_NAME_MAX) ||
        (module_bytes == NULL) || (module_size == 0U) ||
        (module_size > (size_t)BORING_ELF_MODULE_MAX_SIZE) ||
        !boring_elf_validate(module_bytes, module_size,
                             (uintptr_t)BOOTSTRAP_PROGRAM_STACK_BASE,
                             BOOTSTRAP_PROGRAM_STACK_SIZE, &validation)) {
        return false;
    }
    for (index = 0U; index < name_length; ++index) {
        if (name[index] == '\0') {
            return false;
        }
    }

    bootstrap_program_clear();
    for (index = 0U; index < name_length; ++index) {
        bootstrap_program.name[index] = name[index];
    }
    bootstrap_program.name[name_length] = '\0';
    bootstrap_program.name_length = name_length;
    bootstrap_program.module_bytes = module_bytes;
    bootstrap_program.module_size = module_size;
    bootstrap_program.registered = true;
    return true;
}

static uint64_t syscall_getpid(void) {
    const struct process *process = process_current();

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    return process->pid;
}

static uint64_t syscall_debug_write(uint64_t user_buffer, uint64_t length) {
    char buffer[BORING_SYSCALL_DEBUG_WRITE_MAX + 1U];
    size_t safe_length;

    if ((length == 0ULL) ||
        (length > (uint64_t)BORING_SYSCALL_DEBUG_WRITE_MAX)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    safe_length = (size_t)length;
    if (!syscall_copy_from_user(&buffer[0], (uintptr_t)user_buffer,
                                safe_length)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }

    buffer[safe_length] = '\0';
    serial_write_string("Syscall DEBUG_WRITE: ");
    serial_write_string(&buffer[0]);
    serial_write_string("\n");
    return length;
}

static uint64_t syscall_console_write(uint64_t user_buffer, uint64_t length) {
    char buffer[BORING_SYSCALL_CONSOLE_IO_MAX];
    size_t safe_length;

    if ((length == 0ULL) ||
        (length > (uint64_t)BORING_SYSCALL_CONSOLE_IO_MAX)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    safe_length = (size_t)length;
    if (!syscall_copy_from_user(&buffer[0], (uintptr_t)user_buffer,
                                safe_length)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }

    serial_write_bytes(&buffer[0], safe_length);
    return length;
}

static uint64_t syscall_console_read(uint64_t user_buffer, uint64_t length) {
    char buffer[BORING_SYSCALL_CONSOLE_IO_MAX];
    size_t safe_length;
    size_t index;

    if ((length == 0ULL) ||
        (length > (uint64_t)BORING_SYSCALL_CONSOLE_IO_MAX)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    safe_length = (size_t)length;
    if (!syscall_user_range_accessible((uintptr_t)user_buffer,
                                       safe_length, true)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }

    for (index = 0U; index < safe_length; ++index) {
        buffer[index] = serial_read_char_blocking();
    }

    if (!syscall_copy_to_user((uintptr_t)user_buffer, &buffer[0],
                              safe_length)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }

    return length;
}

static uint64_t syscall_launch_rollback(struct process *child,
                                        struct boring_elf_image *image,
                                        bool image_loaded,
                                        int error_number) {
    if (image_loaded && ((image == NULL) || !boring_elf_unload(image))) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if ((child != NULL) && !process_discard_unstarted(child)) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    return syscall_error(error_number);
}

static uint64_t syscall_launch(struct x86_64_syscall_frame *frame,
                               uint64_t user_name,
                               uint64_t length) {
    char name[BORING_SYSCALL_LAUNCH_NAME_MAX + 1U];
    struct process *const parent = process_current();
    struct process *child = NULL;
    struct vfs_path parent_cwd = { NULL, NULL };
    struct boring_elf_image image;
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;
    int copy_error;
    bool image_loaded = false;

    if ((frame == NULL) || (parent == NULL) || !process_is_alive(parent) ||
        (parent->pid != 1ULL)) {
        return syscall_error(BORING_SYSCALL_EACCES);
    }

    copy_error = syscall_copy_explicit_string(
        user_name, length, (size_t)BORING_SYSCALL_LAUNCH_NAME_MAX, name);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    if (!bootstrap_program.registered) {
        return syscall_error(BORING_SYSCALL_ENOTSUP);
    }
    if (!bootstrap_program_name_equals(name, (size_t)length)) {
        return syscall_error(BORING_SYSCALL_ENOENT);
    }
    if (!process_get_cwd(parent, &parent_cwd)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    if (!process_create(&child) || (child == NULL)) {
        (void)vfs_path_release(&parent_cwd);
        return syscall_error(BORING_SYSCALL_ENOSPC);
    }
    if (!process_set_cwd(child, &parent_cwd)) {
        (void)vfs_path_release(&parent_cwd);
        return syscall_launch_rollback(child, &image, false,
                                       BORING_SYSCALL_EIO);
    }
    if (vfs_path_release(&parent_cwd) != VFS_RESULT_OK) {
        return syscall_launch_rollback(child, &image, false,
                                       BORING_SYSCALL_EIO);
    }

    if (!boring_elf_load(child, bootstrap_program.module_bytes,
                         bootstrap_program.module_size,
                         (uintptr_t)BOOTSTRAP_PROGRAM_STACK_BASE,
                         BOOTSTRAP_PROGRAM_STACK_SIZE, &image)) {
        return syscall_launch_rollback(child, &image, false,
                                       BORING_SYSCALL_EIO);
    }
    image_loaded = true;

    if ((child->address_space.root_physical ==
         parent->address_space.root_physical) ||
        !ring3_user_query_mapping(&child->address_space, image.entry,
                                  &entry_info) ||
        !entry_info.executable || entry_info.writable ||
        !ring3_user_query_mapping(&child->address_space, image.stack_base,
                                  &stack_info) ||
        !stack_info.writable || stack_info.executable ||
        !ring3_shared_higher_half_supervisor_only(&child->address_space) ||
        !process_is_alive(parent)) {
        return syscall_launch_rollback(child, &image, image_loaded,
                                       BORING_SYSCALL_EIO);
    }

    serial_write_string("boring-launch: caller pid ");
    serial_write_u64(parent->pid);
    serial_write_string("\nboring-launch: child pid ");
    serial_write_u64(child->pid);
    serial_write_string("\nboring-launch: child root ");
    serial_write_hex_u64(child->address_space.root_physical);
    serial_write_string("\nboring-launch: independent address space\n");
    serial_write_string("boring-launch: shell entry executable\n");
    serial_write_string("boring-launch: shell stack rw-nx\n");
    serial_write_string("boring-launch: higher-half supervisor-only\n");
    serial_write_string("boring-launch: cwd inherited\n");
    serial_write_string("boring-launch: pid 1 remains alive\n");

    frame->user_rsp = (uint64_t)image.stack_top;
    frame->user_rip = (uint64_t)image.entry;
    frame->rbx = 0ULL;
    frame->rbp = 0ULL;
    frame->r12 = 0ULL;
    frame->r13 = 0ULL;
    frame->r14 = 0ULL;
    frame->r15 = 0ULL;
    frame->rdi = 0ULL;
    frame->rsi = 0ULL;
    frame->rdx = 0ULL;
    frame->r10 = 0ULL;
    frame->r8 = 0ULL;
    frame->r9 = 0ULL;
    frame->reserved = 0ULL;

    if (!process_activate(child)) {
        return syscall_launch_rollback(child, &image, image_loaded,
                                       BORING_SYSCALL_EIO);
    }

    serial_write_string("boring-launch: handoff via SYSRETQ\n");
    return 0ULL;
}

static uint64_t syscall_fs_mkdir(uint64_t user_name, uint64_t length) {
    char name[VFS_NAME_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path cwd = { NULL, NULL };
    struct vfs_path created = { NULL, NULL };
    enum vfs_result result;
    int copy_error;

    copy_error = syscall_copy_explicit_string(user_name, length,
                                              (size_t)VFS_NAME_MAX, name);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    if ((process == NULL) || !process_get_cwd(process, &cwd)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    result = vfs_mkdir_at(&cwd, name, &created);
    if (result == VFS_RESULT_OK) {
        if (vfs_path_release(&created) != VFS_RESULT_OK) {
            (void)vfs_path_release(&cwd);
            return syscall_error(BORING_SYSCALL_EIO);
        }
    }
    if (vfs_path_release(&cwd) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    return 0ULL;
}

static uint64_t syscall_fs_rmdir(uint64_t user_name, uint64_t length) {
    char name[VFS_NAME_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path cwd = { NULL, NULL };
    enum vfs_result result;
    int copy_error;

    copy_error = syscall_copy_explicit_string(user_name, length,
                                              (size_t)VFS_NAME_MAX, name);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    if ((process == NULL) || !process_get_cwd(process, &cwd)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    result = vfs_rmdir_at(&cwd, name);
    if (vfs_path_release(&cwd) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    return 0ULL;
}

static uint64_t syscall_fs_chdir(uint64_t user_path, uint64_t length) {
    char path[VFS_PATH_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path resolved = { NULL, NULL };
    enum vfs_result result;
    int copy_error;

    copy_error = syscall_copy_explicit_string(user_path, length,
                                              (size_t)VFS_PATH_MAX, path);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    result = vfs_resolve_process(process, path, &resolved);
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    if (!vfs_path_is_directory(&resolved)) {
        (void)vfs_path_release(&resolved);
        return syscall_error(BORING_SYSCALL_ENOTDIR);
    }
    if (!process_set_cwd(process, &resolved)) {
        (void)vfs_path_release(&resolved);
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if (vfs_path_release(&resolved) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    return 0ULL;
}

static uint64_t syscall_fs_readdir(uint64_t user_path,
                                   uint64_t length,
                                   uint64_t index,
                                   uint64_t user_entry) {
    char path[VFS_PATH_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path directory = { NULL, NULL };
    struct vfs_dirent kernel_entry;
    struct boring_dirent entry = { 0ULL, 0U, 0U, { 0 } };
    enum vfs_result result;
    int copy_error;
    size_t name_index;

    if (!syscall_user_range_accessible((uintptr_t)user_entry,
                                       sizeof(entry), true)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    copy_error = syscall_copy_explicit_string(user_path, length,
                                              (size_t)VFS_PATH_MAX, path);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    result = vfs_resolve_process(process, path, &directory);
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    if (!vfs_path_is_directory(&directory)) {
        (void)vfs_path_release(&directory);
        return syscall_error(BORING_SYSCALL_ENOTDIR);
    }

    result = vfs_readdir_path(&directory, index, &kernel_entry);
    if (result == VFS_RESULT_NOT_FOUND) {
        if (vfs_path_release(&directory) != VFS_RESULT_OK) {
            return syscall_error(BORING_SYSCALL_EIO);
        }
        return 0ULL;
    }
    if (result != VFS_RESULT_OK) {
        (void)vfs_path_release(&directory);
        return syscall_error(syscall_vfs_error(result));
    }
    if ((kernel_entry.name_length > (size_t)VFS_NAME_MAX) ||
        (kernel_entry.name[kernel_entry.name_length] != '\0') ||
        ((kernel_entry.type != VFS_NODE_DIRECTORY) &&
         (kernel_entry.type != VFS_NODE_REGULAR))) {
        (void)vfs_path_release(&directory);
        return syscall_error(BORING_SYSCALL_EIO);
    }

    entry.node_id = kernel_entry.node_id;
    entry.type = (uint32_t)kernel_entry.type;
    entry.name_length = (uint32_t)kernel_entry.name_length;
    for (name_index = 0U; name_index < kernel_entry.name_length;
         ++name_index) {
        entry.name[name_index] = kernel_entry.name[name_index];
    }
    if (vfs_path_release(&directory) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if (!syscall_copy_to_user((uintptr_t)user_entry, &entry, sizeof(entry))) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    return 1ULL;
}

bool syscall_console_safety_self_test(uintptr_t read_only_user_address,
                                      uintptr_t unmapped_user_address) {
    const uint64_t kernel_pointer = 0xffff800000000000ULL;
    const uint64_t overflow_pointer = UINT64_MAX - 1ULL;
    const uint64_t oversized =
        (uint64_t)BORING_SYSCALL_CONSOLE_IO_MAX + 1ULL;
    const uint64_t efault = syscall_error(BORING_SYSCALL_EFAULT);
    const uint64_t einval = syscall_error(BORING_SYSCALL_EINVAL);

    if (!syscall_initialized ||
        (syscall_console_write(0ULL, 1ULL) != efault) ||
        (syscall_console_write(kernel_pointer, 1ULL) != efault) ||
        (syscall_console_write((uint64_t)unmapped_user_address, 1ULL) != efault) ||
        (syscall_console_write(overflow_pointer, 4ULL) != efault) ||
        (syscall_console_write((uint64_t)read_only_user_address, 0ULL) != einval) ||
        (syscall_console_write((uint64_t)read_only_user_address, oversized) != einval) ||
        (syscall_console_read(0ULL, 1ULL) != efault) ||
        (syscall_console_read(kernel_pointer, 1ULL) != efault) ||
        (syscall_console_read((uint64_t)unmapped_user_address, 1ULL) != efault) ||
        (syscall_console_read((uint64_t)read_only_user_address, 1ULL) != efault) ||
        (syscall_console_read(overflow_pointer, 4ULL) != efault) ||
        (syscall_console_read((uint64_t)unmapped_user_address, 0ULL) != einval) ||
        (syscall_console_read((uint64_t)unmapped_user_address, oversized) != einval)) {
        return false;
    }

    return true;
}

static bool syscall_return_state_valid(struct x86_64_syscall_frame *frame) {
    struct process *process;
    struct descriptor_stats descriptors;
    uint64_t translated;

    if ((frame == NULL) || !descriptor_get_stats(&descriptors) ||
        (descriptors.kernel_code_selector !=
         (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR) ||
        (descriptors.kernel_data_selector !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR) ||
        (descriptors.user_code_selector !=
         (uint16_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (descriptors.user_data_selector !=
         (uint16_t)X86_64_GDT_USER_DATA_SELECTOR) ||
        !ring3_user_range_valid((uintptr_t)frame->user_rip, 1U) ||
        (frame->user_rsp == 0ULL) ||
        !ring3_user_range_valid((uintptr_t)(frame->user_rsp - 1ULL), 1U)) {
        return false;
    }

    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap ||
        !ring3_user_translate(&process->address_space,
                              (uintptr_t)frame->user_rip, false,
                              &translated) ||
        !ring3_user_translate(&process->address_space,
                              (uintptr_t)(frame->user_rsp - 1ULL), true,
                              &translated)) {
        return false;
    }

    frame->user_rflags = sanitize_user_rflags(frame->user_rflags);
    return ((frame->user_rflags & (RFLAGS_IOPL | RFLAGS_NT | RFLAGS_VM |
                                   RFLAGS_TF | RFLAGS_IF | RFLAGS_DF |
                                   RFLAGS_AC)) == 0ULL) &&
           ((frame->user_rflags & RFLAGS_RESERVED1) != 0ULL);
}

bool syscall_init(void) {
    struct descriptor_stats descriptors;
    const uintptr_t stack_base = (uintptr_t)&x86_64_syscall_stack[0];
    const uintptr_t stack_top =
        stack_base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;
    const uint64_t lstar = (uint64_t)(uintptr_t)&x86_64_syscall_entry;
    uint64_t efer;
    uint64_t active_efer;
    uint64_t active_star;
    uint64_t active_lstar;
    uint64_t active_fmask;

    if (syscall_initialized) {
        return true;
    }

    syscall_state.supported = false;
    syscall_state.initialized = false;
    syscall_state.dispatch_count = 0ULL;
    syscall_state.last_kernel_rsp = 0U;
    syscall_state.last_user_rsp = 0U;
    bootstrap_program_clear();

    if (!x86_64_syscall_supported() || !descriptor_get_stats(&descriptors) ||
        (descriptors.kernel_code_selector !=
         (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR) ||
        (descriptors.kernel_data_selector !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR) ||
        (descriptors.user_code_selector !=
         (uint16_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (descriptors.user_data_selector !=
         (uint16_t)X86_64_GDT_USER_DATA_SELECTOR) ||
        ((stack_base & 0x0fU) != 0U) || ((stack_top & 0x0fU) != 0U) ||
        ((lstar >> 48U) != 0xffffULL)) {
        return false;
    }

    syscall_state.supported = true;
    efer = x86_64_read_msr(IA32_EFER);

    x86_64_write_msr(IA32_STAR, STAR_VALUE);
    x86_64_write_msr(IA32_LSTAR, lstar);
    x86_64_write_msr(IA32_FMASK, SYSCALL_FMASK_VALUE);
    x86_64_write_msr(IA32_EFER, efer | EFER_SCE);

    active_efer = x86_64_read_msr(IA32_EFER);
    active_star = x86_64_read_msr(IA32_STAR);
    active_lstar = x86_64_read_msr(IA32_LSTAR);
    active_fmask = x86_64_read_msr(IA32_FMASK);

    if (((active_efer & EFER_SCE) == 0ULL) ||
        (active_star != STAR_VALUE) || (active_lstar != lstar) ||
        (active_fmask != SYSCALL_FMASK_VALUE) ||
        ((active_efer & ~EFER_SCE) != (efer & ~EFER_SCE))) {
        return false;
    }

    syscall_state.efer = active_efer;
    syscall_state.star = active_star;
    syscall_state.lstar = active_lstar;
    syscall_state.fmask = active_fmask;
    syscall_state.stack_base = stack_base;
    syscall_state.stack_top = stack_top;
    syscall_state.initialized = true;
    syscall_initialized = true;
    return true;
}

bool syscall_get_stats(struct syscall_stats *stats) {
    if ((stats == NULL) || !syscall_initialized) {
        return false;
    }

    *stats = syscall_state;
    return true;
}

void x86_64_syscall_dispatch(struct x86_64_syscall_frame *frame) {
    const uintptr_t live_rsp = x86_64_read_rsp();
    const uintptr_t frame_address = (uintptr_t)frame;
    uint64_t result;

    if (!syscall_initialized || (frame == NULL) ||
        !syscall_stack_contains(live_rsp) ||
        !syscall_stack_contains(frame_address) ||
        (frame_address > syscall_state.stack_top -
                         (uintptr_t)sizeof(*frame))) {
        syscall_fatal("invalid trusted entry stack");
    }

    syscall_state.last_kernel_rsp = live_rsp;
    syscall_state.last_user_rsp = (uintptr_t)frame->user_rsp;
    ++syscall_state.dispatch_count;

    switch (frame->syscall_number) {
        case BORING_SYS_GETPID:
            result = syscall_getpid();
            break;
        case BORING_SYS_DEBUG_WRITE:
            result = syscall_debug_write(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_CONSOLE_WRITE:
            result = syscall_console_write(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_CONSOLE_READ:
            result = syscall_console_read(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_LAUNCH:
            result = syscall_launch(frame, frame->rdi, frame->rsi);
            break;
        case BORING_SYS_FS_READDIR:
            result = syscall_fs_readdir(frame->rdi, frame->rsi,
                                        frame->rdx, frame->r10);
            break;
        case BORING_SYS_FS_MKDIR:
            result = syscall_fs_mkdir(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_FS_RMDIR:
            result = syscall_fs_rmdir(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_FS_CHDIR:
            result = syscall_fs_chdir(frame->rdi, frame->rsi);
            break;
        default:
            result = syscall_error(BORING_SYSCALL_ENOSYS);
            break;
    }

    frame->result = result;
    if (!syscall_return_state_valid(frame)) {
        syscall_fatal("invalid SYSRETQ user return state");
    }
}
