#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_loader.h>
#include <boring/elf_vfs.h>
#include <boring/event_syscall.h>
#include <boring/fd.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/m36_syscall.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>
#include <boring/task.h>
#include <boring/user_memory.h>
#include <boring/vfs.h>
#include <boring/vmm.h>

#define M36_PROGRAM_STACK_BASE 0x0000000040010000ULL
#define M36_PROGRAM_STACK_SIZE ((size_t)BORING_ELF_PAGE_SIZE)
#define M36_ARG_RUNTIME_RESERVE 2048U
#define M36_ARG_POINTER_BYTES \
    (((size_t)BORING_SYSCALL_ARG_MAX + 1U) * sizeof(uint64_t))
#define M36_ARG_BLOCK_MAX \
    (M36_ARG_POINTER_BYTES + (size_t)BORING_SYSCALL_ARG_BYTES_MAX + 15U)

struct m36_launch_arguments {
    size_t argc;
    size_t total_bytes;
    uint16_t offsets[BORING_SYSCALL_ARG_MAX];
    char data[BORING_SYSCALL_ARG_BYTES_MAX];
};

struct m36_spawn_record {
    struct process *child;
    struct boring_elf_image image;
    uintptr_t entry_rsp;
    uintptr_t argv;
    uint64_t parent_pid;
    int32_t exit_status;
    size_t argc;
    bool active;
    bool image_loaded;
    bool exited;
    bool detached;
    bool parent_waiting;
};

static struct m36_spawn_record spawn_records[KERNEL_PROCESS_MAX];

void x86_64_enter_ring3_argv(uintptr_t user_rip,
                             uintptr_t user_rsp,
                             uint16_t user_cs,
                             uint16_t user_ss,
                             size_t argc,
                             uintptr_t argv) __attribute__((noreturn));

static uint64_t m36_error(int error_number) {
    return (uint64_t)(-(int64_t)error_number);
}

static void m36_zero(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;

    for (index = 0U; index < size; ++index) {
        bytes[index] = 0U;
    }
}

static bool m36_user_range(struct process *process,
                           uintptr_t address,
                           size_t size,
                           bool writable) {
    size_t done = 0U;

    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap ||
        !ring3_user_range_valid(address, size)) {
        return false;
    }
    while (done < size) {
        uint64_t physical;
        size_t chunk = (size_t)VMM_PAGE_SIZE -
            (size_t)((address + done) & (VMM_PAGE_SIZE - 1ULL));

        if (chunk > size - done) {
            chunk = size - done;
        }
        if (!ring3_user_translate(&process->address_space, address + done,
                                  writable, &physical)) {
            return false;
        }
        done += chunk;
    }
    return true;
}

static bool m36_copy_process(struct process *process,
                             uintptr_t address,
                             void *buffer,
                             size_t size,
                             bool to_process) {
    struct vmm_stats stats;
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0U;

    if (!m36_user_range(process, address, size, to_process) ||
        !vmm_get_stats(&stats)) {
        return false;
    }
    while (done < size) {
        uint64_t physical;
        uint8_t *mapped;
        size_t index;
        size_t chunk = (size_t)VMM_PAGE_SIZE -
            (size_t)((address + done) & (VMM_PAGE_SIZE - 1ULL));

        if (chunk > size - done) {
            chunk = size - done;
        }
        if (!ring3_user_translate(&process->address_space, address + done,
                                  to_process, &physical) ||
            (physical > UINT64_MAX - stats.hhdm_offset)) {
            return false;
        }
        mapped = (uint8_t *)(uintptr_t)(stats.hhdm_offset + physical);
        for (index = 0U; index < chunk; ++index) {
            if (to_process) {
                mapped[index] = bytes[done + index];
            } else {
                bytes[done + index] = mapped[index];
            }
        }
        done += chunk;
    }
    return true;
}

static bool m36_copy_from_user(void *buffer, uintptr_t address, size_t size) {
    return m36_copy_process(process_current(), address, buffer, size, false);
}

static bool m36_copy_to_user(uintptr_t address, const void *buffer, size_t size) {
    return m36_copy_process(process_current(), address, (void *)buffer, size,
                            true);
}

static int m36_copy_string(uint64_t user_address,
                           uint64_t raw_length,
                           size_t maximum,
                           char *buffer) {
    size_t length;
    size_t index;

    if ((raw_length == 0ULL) || (raw_length > (uint64_t)maximum) ||
        (raw_length > (uint64_t)SIZE_MAX) || (buffer == NULL)) {
        return (raw_length > (uint64_t)maximum) ?
            BORING_SYSCALL_ENAMETOOLONG : BORING_SYSCALL_EINVAL;
    }
    length = (size_t)raw_length;
    if (!m36_copy_from_user(buffer, (uintptr_t)user_address, length)) {
        return BORING_SYSCALL_EFAULT;
    }
    for (index = 0U; index < length; ++index) {
        if (buffer[index] == '\0') {
            return BORING_SYSCALL_EINVAL;
        }
    }
    buffer[length] = '\0';
    return 0;
}

static int m36_copy_arguments(uint64_t user_argv,
                              uint64_t raw_argc,
                              struct m36_launch_arguments *arguments) {
    uint64_t pointers[BORING_SYSCALL_ARG_MAX];
    size_t argc;
    size_t index;

    if ((arguments == NULL) || (raw_argc == 0ULL) ||
        (raw_argc > (uint64_t)BORING_SYSCALL_ARG_MAX) ||
        (raw_argc > (uint64_t)SIZE_MAX)) {
        return BORING_SYSCALL_EINVAL;
    }
    argc = (size_t)raw_argc;
    m36_zero(arguments, sizeof(*arguments));
    if (!m36_copy_from_user(pointers, (uintptr_t)user_argv,
                            argc * sizeof(pointers[0]))) {
        return BORING_SYSCALL_EFAULT;
    }
    arguments->argc = argc;
    for (index = 0U; index < argc; ++index) {
        size_t length = 0U;
        const uint64_t pointer = pointers[index];

        if (pointer == 0ULL) {
            return BORING_SYSCALL_EFAULT;
        }
        if (arguments->total_bytes > (size_t)UINT16_MAX) {
            return BORING_SYSCALL_EINVAL;
        }
        arguments->offsets[index] = (uint16_t)arguments->total_bytes;
        for (;;) {
            char value;
            uint64_t address;

            if (arguments->total_bytes >=
                (size_t)BORING_SYSCALL_ARG_BYTES_MAX) {
                return BORING_SYSCALL_ENAMETOOLONG;
            }
            if ((uint64_t)length > UINT64_MAX - pointer) {
                return BORING_SYSCALL_EFAULT;
            }
            address = pointer + (uint64_t)length;
            if (!m36_copy_from_user(&value, (uintptr_t)address, 1U)) {
                return BORING_SYSCALL_EFAULT;
            }
            arguments->data[arguments->total_bytes] = value;
            ++arguments->total_bytes;
            if (value == '\0') {
                break;
            }
            ++length;
        }
    }
    return 0;
}

static void m36_store_u64(uint8_t *destination, uint64_t value) {
    size_t index;

    for (index = 0U; index < sizeof(value); ++index) {
        destination[index] = (uint8_t)(value >> (index * 8U));
    }
}

static bool m36_prepare_arguments(struct process *child,
                                  const struct boring_elf_image *image,
                                  const struct m36_launch_arguments *arguments,
                                  uintptr_t *rsp_out,
                                  uintptr_t *argv_out) {
    uint8_t block[M36_ARG_BLOCK_MAX];
    size_t pointer_bytes;
    size_t block_size;
    size_t aligned_size;
    uintptr_t rsp;
    size_t index;

    if ((child == NULL) || (image == NULL) || (arguments == NULL) ||
        (rsp_out == NULL) || (argv_out == NULL) ||
        (arguments->argc == 0U) ||
        (arguments->argc > (size_t)BORING_SYSCALL_ARG_MAX)) {
        return false;
    }
    pointer_bytes = (arguments->argc + 1U) * sizeof(uint64_t);
    if ((pointer_bytes > sizeof(block)) ||
        (arguments->total_bytes > sizeof(block) - pointer_bytes)) {
        return false;
    }
    block_size = pointer_bytes + arguments->total_bytes;
    if (block_size > SIZE_MAX - 15U) {
        return false;
    }
    aligned_size = (block_size + 15U) & ~(size_t)15U;
    if ((aligned_size > M36_PROGRAM_STACK_SIZE) ||
        ((size_t)M36_ARG_RUNTIME_RESERVE >
         M36_PROGRAM_STACK_SIZE - aligned_size)) {
        return false;
    }
    rsp = image->stack_top - (uintptr_t)aligned_size;
    if ((rsp & 0x0fU) != 0U) {
        return false;
    }
    m36_zero(block, sizeof(block));
    for (index = 0U; index < arguments->argc; ++index) {
        const uintptr_t string_address =
            rsp + (uintptr_t)pointer_bytes +
            (uintptr_t)arguments->offsets[index];
        m36_store_u64(&block[index * sizeof(uint64_t)],
                      (uint64_t)string_address);
    }
    for (index = 0U; index < arguments->total_bytes; ++index) {
        block[pointer_bytes + index] =
            (uint8_t)arguments->data[index];
    }
    if (!m36_copy_process(child, rsp, block, block_size, true)) {
        return false;
    }
    *rsp_out = rsp;
    *argv_out = rsp;
    return true;
}

static const char *m36_basename(const char *path) {
    const char *base = path;
    size_t index;

    if (path == NULL) {
        return NULL;
    }
    for (index = 0U; path[index] != '\0'; ++index) {
        if ((path[index] == '/') && (path[index + 1U] != '\0')) {
            base = &path[index + 1U];
        }
    }
    return base;
}

static size_t m36_text_length(const char *text, size_t maximum) {
    size_t length = 0U;

    if (text == NULL) {
        return maximum + 1U;
    }
    while ((length <= maximum) && (text[length] != '\0')) {
        ++length;
    }
    return length;
}

static int m36_vfs_error(enum vfs_result result) {
    switch (result) {
        case VFS_RESULT_OK: return 0;
        case VFS_RESULT_NOT_FOUND: return BORING_SYSCALL_ENOENT;
        case VFS_RESULT_ALREADY_EXISTS: return BORING_SYSCALL_EEXIST;
        case VFS_RESULT_NOT_DIRECTORY: return BORING_SYSCALL_ENOTDIR;
        case VFS_RESULT_NOT_REGULAR: return BORING_SYSCALL_EISDIR;
        case VFS_RESULT_NOT_EMPTY: return BORING_SYSCALL_ENOTEMPTY;
        case VFS_RESULT_NO_SPACE: return BORING_SYSCALL_ENOSPC;
        case VFS_RESULT_BUSY:
        case VFS_RESULT_ALREADY_MOUNTED:
        case VFS_RESULT_MOUNT_CONFLICT: return BORING_SYSCALL_EBUSY;
        case VFS_RESULT_PATH_TOO_LONG:
        case VFS_RESULT_NAME_TOO_LONG: return BORING_SYSCALL_ENAMETOOLONG;
        case VFS_RESULT_ACCESS_DENIED: return BORING_SYSCALL_EACCES;
        case VFS_RESULT_NOT_SUPPORTED:
        case VFS_RESULT_CROSS_FILESYSTEM: return BORING_SYSCALL_ENOTSUP;
        case VFS_RESULT_INVALID_ARGUMENT:
        case VFS_RESULT_EMPTY_PATH:
        case VFS_RESULT_NO_CWD:
        case VFS_RESULT_OVERFLOW: return BORING_SYSCALL_EINVAL;
        case VFS_RESULT_NOT_INITIALIZED:
        case VFS_RESULT_ALREADY_INITIALIZED:
        case VFS_RESULT_CORRUPT:
        default: return BORING_SYSCALL_EIO;
    }
}

static void m36_record_clear(struct m36_spawn_record *record) {
    if (record != NULL) {
        m36_zero(record, sizeof(*record));
    }
}

static struct m36_spawn_record *m36_record_free(void) {
    size_t index;

    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        if (!spawn_records[index].active) {
            return &spawn_records[index];
        }
    }
    return NULL;
}

static struct m36_spawn_record *m36_record_child(struct process *child) {
    size_t index;

    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        if (spawn_records[index].active &&
            (spawn_records[index].child == child)) {
            return &spawn_records[index];
        }
    }
    return NULL;
}

static struct m36_spawn_record *m36_record_pid(uint64_t pid,
                                                uint64_t parent_pid) {
    size_t index;

    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        if (spawn_records[index].active &&
            (spawn_records[index].child != NULL) &&
            (spawn_records[index].child->pid == pid) &&
            (spawn_records[index].parent_pid == parent_pid)) {
            return &spawn_records[index];
        }
    }
    return NULL;
}

static bool m36_reap_record(struct m36_spawn_record *record) {
    struct process *child;

    if ((record == NULL) || !record->active || (record->child == NULL) ||
        (record->child->state != PROCESS_FINISHED)) {
        return false;
    }
    child = record->child;
    if (!task_reap_finished_process(child) || !process_destroy(child)) {
        return false;
    }
    m36_record_clear(record);
    return true;
}

static void m36_reap_detached(void) {
    size_t index;

    for (index = 0U; index < (size_t)KERNEL_PROCESS_MAX; ++index) {
        struct m36_spawn_record *const record = &spawn_records[index];

        if (record->active && record->detached && record->exited &&
            (record->child != NULL) &&
            (record->child->state == PROCESS_FINISHED)) {
            if (!m36_reap_record(record)) {
                serial_write_string("m36: detached reap deferred\n");
            }
        }
    }
}

static uint64_t m36_pty_create(uint64_t user_result) {
    struct process *const process = process_current();
    struct boring_pty_create_result result;
    struct pty_handle master;
    struct pty_handle slave;
    uint32_t master_fd = UINT32_MAX;
    uint32_t slave_fd = UINT32_MAX;
    const uint32_t access = VFS_ACCESS_READ | VFS_ACCESS_WRITE;

    if ((process == NULL) || !process_is_alive(process) ||
        !m36_user_range(process, (uintptr_t)user_result, sizeof(result), true)) {
        return m36_error(BORING_SYSCALL_EFAULT);
    }
    if (pty_create_pair(&master, &slave) != PTY_RESULT_OK) {
        return m36_error(BORING_SYSCALL_ENOSPC);
    }
    if (kernel_fd_install_pty(&process->fd_table, master, access,
                              &master_fd) != VFS_RESULT_OK) {
        (void)pty_close(master);
        (void)pty_close(slave);
        return m36_error(BORING_SYSCALL_ENOSPC);
    }
    if (kernel_fd_install_pty(&process->fd_table, slave, access,
                              &slave_fd) != VFS_RESULT_OK) {
        (void)kernel_fd_close(&process->fd_table, master_fd);
        (void)pty_close(master);
        (void)pty_close(slave);
        return m36_error(BORING_SYSCALL_ENOSPC);
    }
    if ((pty_close(master) != PTY_RESULT_OK) ||
        (pty_close(slave) != PTY_RESULT_OK)) {
        (void)kernel_fd_close(&process->fd_table, master_fd);
        (void)kernel_fd_close(&process->fd_table, slave_fd);
        return m36_error(BORING_SYSCALL_EIO);
    }
    result.master_fd = master_fd;
    result.slave_fd = slave_fd;
    if (!m36_copy_to_user((uintptr_t)user_result, &result, sizeof(result))) {
        (void)kernel_fd_close(&process->fd_table, master_fd);
        (void)kernel_fd_close(&process->fd_table, slave_fd);
        return m36_error(BORING_SYSCALL_EFAULT);
    }
    return 0ULL;
}

static uint64_t m36_spawn_rollback(struct process *child,
                                   struct boring_elf_image *image,
                                   bool image_loaded,
                                   int error_number) {
    if (image_loaded && ((image == NULL) || !boring_elf_unload(image))) {
        return m36_error(BORING_SYSCALL_EIO);
    }
    if ((child != NULL) && !process_discard_unstarted(child)) {
        return m36_error(BORING_SYSCALL_EIO);
    }
    return m36_error(error_number);
}

static void m36_spawn_entry(void *state) {
    struct m36_spawn_record *const record =
        (struct m36_spawn_record *)state;

    if ((record == NULL) || !record->active || (record->child == NULL) ||
        (process_current() != record->child) ||
        !process_is_alive(record->child) || !record->image_loaded) {
        task_exit_current_process();
    }
    x86_64_enter_ring3_argv(record->image.entry, record->entry_rsp,
                            (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                            (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                            record->argc, record->argv);
}

static uint64_t m36_spawn(uint64_t user_path,
                          uint64_t path_length,
                          uint64_t user_argv,
                          uint64_t raw_argc,
                          uint64_t user_stdio) {
    char path[BORING_SYSCALL_EXEC_PATH_MAX + 1U];
    struct m36_launch_arguments arguments;
    struct boring_spawn_stdio stdio_config;
    struct process *const parent = process_current();
    struct process *child = NULL;
    struct vfs_path parent_cwd = { NULL, NULL };
    struct boring_elf_vfs_source source = {
        { 0ULL, NULL, NULL }, { { NULL, NULL }, 0ULL, 0U, false }, false
    };
    struct boring_elf_image image;
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;
    struct m36_spawn_record *record;
    const char *name;
    uintptr_t entry_rsp = 0U;
    uintptr_t child_argv = 0U;
    uint64_t task_id = 0ULL;
    enum vfs_result vfs_result;
    int copy_error;
    bool image_loaded = false;

    if ((parent == NULL) || !process_is_alive(parent) || (parent->pid == 0ULL)) {
        return m36_error(BORING_SYSCALL_EACCES);
    }
    record = m36_record_free();
    if (record == NULL) {
        return m36_error(BORING_SYSCALL_ENOSPC);
    }
    copy_error = m36_copy_string(user_path, path_length,
                                 (size_t)BORING_SYSCALL_EXEC_PATH_MAX, path);
    if (copy_error != 0) {
        return m36_error(copy_error);
    }
    copy_error = m36_copy_arguments(user_argv, raw_argc, &arguments);
    if (copy_error != 0) {
        return m36_error(copy_error);
    }
    if (!m36_copy_from_user(&stdio_config, (uintptr_t)user_stdio,
                            sizeof(stdio_config))) {
        return m36_error(BORING_SYSCALL_EFAULT);
    }
    if ((stdio_config.flags & ~BORING_SPAWN_FLAG_MASK) != 0U) {
        return m36_error(BORING_SYSCALL_EINVAL);
    }
    name = m36_basename(path);
    if ((name == NULL) || (name[0] == '\0') ||
        (m36_text_length(name, (size_t)KERNEL_PROCESS_NAME_MAX) >
         (size_t)KERNEL_PROCESS_NAME_MAX)) {
        return m36_error(BORING_SYSCALL_ENAMETOOLONG);
    }
    vfs_result = boring_elf_vfs_source_open(parent, path, &source);
    if (vfs_result != VFS_RESULT_OK) {
        return m36_error((vfs_result == VFS_RESULT_OVERFLOW) ?
            BORING_SYSCALL_ENOEXEC : m36_vfs_error(vfs_result));
    }
    if (!process_get_cwd(parent, &parent_cwd)) {
        (void)boring_elf_vfs_source_close(&source);
        return m36_error(BORING_SYSCALL_EINVAL);
    }
    if (!process_create(&child) || (child == NULL)) {
        (void)vfs_path_release(&parent_cwd);
        (void)boring_elf_vfs_source_close(&source);
        return m36_error(BORING_SYSCALL_ENOSPC);
    }
    if (!process_set_name(child, name) ||
        !process_set_cwd(child, &parent_cwd)) {
        (void)vfs_path_release(&parent_cwd);
        (void)boring_elf_vfs_source_close(&source);
        return m36_spawn_rollback(child, &image, false,
                                  BORING_SYSCALL_EIO);
    }
    if (vfs_path_release(&parent_cwd) != VFS_RESULT_OK) {
        (void)boring_elf_vfs_source_close(&source);
        return m36_spawn_rollback(child, &image, false,
                                  BORING_SYSCALL_EIO);
    }
    if ((kernel_fd_clone_stdio(&parent->fd_table, stdio_config.stdin_fd,
                               &child->fd_table, KERNEL_FD_STDIN,
                               VFS_ACCESS_READ) != VFS_RESULT_OK) ||
        (kernel_fd_clone_stdio(&parent->fd_table, stdio_config.stdout_fd,
                               &child->fd_table, KERNEL_FD_STDOUT,
                               VFS_ACCESS_WRITE) != VFS_RESULT_OK) ||
        (kernel_fd_clone_stdio(&parent->fd_table, stdio_config.stderr_fd,
                               &child->fd_table, KERNEL_FD_STDERR,
                               VFS_ACCESS_WRITE) != VFS_RESULT_OK)) {
        (void)boring_elf_vfs_source_close(&source);
        return m36_spawn_rollback(child, &image, false,
                                  BORING_SYSCALL_EACCES);
    }
    if (!boring_elf_load_source(child, &source.source,
                                (uintptr_t)M36_PROGRAM_STACK_BASE,
                                M36_PROGRAM_STACK_SIZE, &image)) {
        (void)boring_elf_vfs_source_close(&source);
        return m36_spawn_rollback(child, &image, false,
                                  BORING_SYSCALL_ENOEXEC);
    }
    image_loaded = true;
    if (boring_elf_vfs_source_close(&source) != VFS_RESULT_OK) {
        return m36_spawn_rollback(child, &image, image_loaded,
                                  BORING_SYSCALL_EIO);
    }
    if (!m36_prepare_arguments(child, &image, &arguments,
                               &entry_rsp, &child_argv) ||
        (child->address_space.root_physical ==
         parent->address_space.root_physical) ||
        !ring3_user_query_mapping(&child->address_space, image.entry,
                                  &entry_info) ||
        !entry_info.executable || entry_info.writable ||
        !ring3_user_query_mapping(&child->address_space, image.stack_base,
                                  &stack_info) ||
        !stack_info.writable || stack_info.executable ||
        !ring3_shared_higher_half_supervisor_only(&child->address_space)) {
        return m36_spawn_rollback(child, &image, image_loaded,
                                  BORING_SYSCALL_EIO);
    }

    m36_record_clear(record);
    record->child = child;
    record->image = image;
    record->entry_rsp = entry_rsp;
    record->argv = child_argv;
    record->parent_pid = parent->pid;
    record->argc = arguments.argc;
    record->active = true;
    record->image_loaded = true;
    record->detached =
        (stdio_config.flags & BORING_SPAWN_FLAG_DETACHED) != 0U;
    if (!task_create_for_process(child, m36_spawn_entry, record, &task_id)) {
        m36_record_clear(record);
        return m36_spawn_rollback(child, &image, image_loaded,
                                  BORING_SYSCALL_ENOSPC);
    }

    serial_write_string("boring-spawn: parent pid ");
    serial_write_u64(parent->pid);
    serial_write_string(" child pid ");
    serial_write_u64(child->pid);
    serial_write_string(" task ");
    serial_write_u64(task_id);
    serial_write_string(record->detached ? " detached\n" : " foreground\n");
    return child->pid;
}

static bool m36_exit(struct x86_64_syscall_frame *frame) {
    struct process *const process = process_current();
    struct m36_spawn_record *record;
    struct user_memory_cleanup_stats memory_cleanup;
    bool input_released = false;

    if ((frame == NULL) ||
        (frame->syscall_number != (uint64_t)BORING_SYS_EXIT) ||
        (process == NULL)) {
        return false;
    }
    record = m36_record_child(process);
    if (record == NULL) {
        return false;
    }
    record->exit_status = (int32_t)frame->rdi;
    (void)boring_input_process_teardown(process->pid, &input_released);
    boring_ipc_process_cleanup(process);
    if (user_memory_process_cleanup(process, &memory_cleanup) !=
        USER_MEMORY_RESULT_OK) {
        serial_write_string("m36: process memory cleanup failed\n");
        x86_64_halt_forever();
    }
    if (record->image_loaded && !boring_elf_unload(&record->image)) {
        serial_write_string("m36: child ELF cleanup failed\n");
        x86_64_halt_forever();
    }
    record->image_loaded = false;
    record->exited = true;
    if (record->parent_waiting) {
        (void)task_wake_pid(record->parent_pid);
    }
    serial_write_string("boring-spawn: child pid ");
    serial_write_u64(process->pid);
    serial_write_string(" exited status ");
    serial_write_u64((uint64_t)(uint32_t)record->exit_status);
    serial_write_string("\n");
    task_exit_current_process();
}

static uint64_t m36_waitpid(uint64_t pid, uint64_t user_status) {
    struct process *const parent = process_current();
    struct m36_spawn_record *record;
    int status;

    if ((parent == NULL) || !process_is_alive(parent) || (pid == 0ULL) ||
        !m36_user_range(parent, (uintptr_t)user_status, sizeof(status), true)) {
        return m36_error(BORING_SYSCALL_EINVAL);
    }
    record = m36_record_pid(pid, parent->pid);
    if ((record == NULL) || record->detached) {
        return m36_error(BORING_SYSCALL_EINVAL);
    }
    while ((record->child != NULL) &&
           (record->child->state != PROCESS_FINISHED)) {
        record->parent_waiting = true;
        if (!task_block_current()) {
            record->parent_waiting = false;
            return m36_error(BORING_SYSCALL_EBUSY);
        }
        record->parent_waiting = false;
    }
    if ((record->child == NULL) ||
        (record->child->state != PROCESS_FINISHED)) {
        return m36_error(BORING_SYSCALL_EIO);
    }
    status = record->exit_status;
    if (!m36_copy_to_user((uintptr_t)user_status, &status, sizeof(status))) {
        return m36_error(BORING_SYSCALL_EFAULT);
    }
    if (!m36_reap_record(record)) {
        return m36_error(BORING_SYSCALL_EIO);
    }
    return pid;
}

void x86_64_syscall_dispatch_m36(struct x86_64_syscall_frame *frame) {
    uint64_t result;
    uint64_t number;

    if (frame == NULL) {
        return;
    }
    number = frame->syscall_number;
    if (m36_exit(frame)) {
        x86_64_halt_forever();
    }

    x86_64_syscall_dispatch_events(frame);
    m36_reap_detached();

    if (number == (uint64_t)BORING_SYS_PTY_CREATE) {
        result = m36_pty_create(frame->rdi);
    } else if (number == (uint64_t)BORING_SYS_SPAWN) {
        result = m36_spawn(frame->rdi, frame->rsi, frame->rdx,
                           frame->r10, frame->r8);
    } else if (number == (uint64_t)BORING_SYS_WAITPID) {
        struct process *const process = process_current();
        if ((process != NULL) &&
            (m36_record_pid(frame->rdi, process->pid) != NULL)) {
            result = m36_waitpid(frame->rdi, frame->rsi);
        } else {
            return;
        }
    } else {
        return;
    }
    frame->result = result;
}
