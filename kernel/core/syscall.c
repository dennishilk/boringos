#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/cpu_inventory.h>
#include <boring/descriptor.h>
#include <boring/elf_loader.h>
#include <boring/elf_vfs.h>
#include <boring/fd.h>
#include <boring/framebuffer.h>
#include <boring/input.h>
#include <boring/process.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/smbios.h>
#include <boring/syscall.h>
#include <boring/timer.h>
#include <boring/task.h>
#include <boring/user_memory.h>
#include <boring/vfs.h>
#include <boring/vmm.h>
#include <boring/virtio_blk.h>

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
#define SYSCALL_LAUNCH_DEPTH_MAX 2U
#define SYSCALL_ARG_RUNTIME_RESERVE 2048U
#define SYSCALL_ARG_POINTER_BYTES (((size_t)BORING_SYSCALL_ARG_MAX + 1U) * sizeof(uint64_t))
#define SYSCALL_ARG_BLOCK_MAX (SYSCALL_ARG_POINTER_BYTES + (size_t)BORING_SYSCALL_ARG_BYTES_MAX + 15U)

struct syscall_bootstrap_program {
    char name[BORING_SYSCALL_LAUNCH_NAME_MAX + 1U];
    size_t name_length;
    const uint8_t *module_bytes;
    size_t module_size;
    bool registered;
};

struct syscall_suspended_launch {
    struct process *parent;
    struct process *child;
    struct x86_64_syscall_frame parent_frame;
    struct boring_elf_image image;
    int32_t exit_status;
    bool active;
    bool child_exited;
    bool image_loaded;
};

struct syscall_launch_arguments {
    size_t argc;
    size_t total_bytes;
    uint16_t offsets[BORING_SYSCALL_ARG_MAX];
    char data[BORING_SYSCALL_ARG_BYTES_MAX];
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
_Static_assert((KERNEL_PROCESS_NAME_MAX + 1U) == BORING_PROCESS_NAME_CAPACITY,
               "process name capacity must match userspace ABI");
_Static_assert(VFS_PATH_MAX == BORING_SYSCALL_CWD_MAX,
               "cwd ABI path bound must match VFS");
_Static_assert(KERNEL_FD_MAX == 16U,
               "M29 descriptor table bound changed");
_Static_assert(KERNEL_FD_STDIN == BORING_FD_STDIN,
               "stdin descriptor ABI mismatch");
_Static_assert(KERNEL_FD_STDOUT == BORING_FD_STDOUT,
               "stdout descriptor ABI mismatch");
_Static_assert(KERNEL_FD_STDERR == BORING_FD_STDERR,
               "stderr descriptor ABI mismatch");
_Static_assert(VFS_ACCESS_READ == BORING_FD_OPEN_READ,
               "descriptor read flag ABI mismatch");
_Static_assert(VFS_ACCESS_WRITE == BORING_FD_OPEN_WRITE,
               "descriptor write flag ABI mismatch");
_Static_assert(USER_MEMORY_PAGE_SIZE == BORING_MEMORY_PAGE_SIZE,
               "M32 page-size ABI mismatch");
_Static_assert(USER_MEMORY_ANON_MAX_BYTES == BORING_MEMORY_ALLOC_MAX_BYTES,
               "M32 anonymous allocation bound mismatch");
_Static_assert(USER_MEMORY_BUFFER_MAX_BYTES == BORING_BUFFER_MAX_BYTES,
               "M32 shared-buffer bound mismatch");
_Static_assert(USER_MEMORY_ALLOCATION_MAX == BORING_MEMORY_ALLOCATION_MAX,
               "M32 allocation-slot bound mismatch");
_Static_assert(USER_MEMORY_BUFFER_HANDLE_MAX == BORING_BUFFER_HANDLE_MAX,
               "M32 handle-slot bound mismatch");
_Static_assert(USER_MEMORY_BUFFER_MAPPING_MAX == BORING_BUFFER_MAPPING_MAX,
               "M32 mapping-slot bound mismatch");
_Static_assert(USER_MEMORY_BUFFER_OBJECT_MAX == BORING_BUFFER_OBJECT_MAX,
               "M32 object-table bound mismatch");

extern void x86_64_syscall_entry(void);

uint8_t x86_64_syscall_stack[X86_64_SYSCALL_STACK_SIZE]
    __attribute__((aligned(16)));
uint64_t x86_64_syscall_user_rsp_scratch __attribute__((aligned(8)));

static struct syscall_stats syscall_state;
static struct syscall_bootstrap_program bootstrap_program;
static struct syscall_suspended_launch suspended_launch[SYSCALL_LAUNCH_DEPTH_MAX];
static size_t suspended_launch_depth;
static bool syscall_initialized;

static bool syscall_user_range_accessible(uintptr_t user_address,
                                          size_t length,
                                          bool require_writable);
static bool syscall_copy_to_user(uintptr_t user_address,
                                 const void *source,
                                 size_t length);

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

static void syscall_zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;

    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static bool syscall_copy_literal(char *destination,
                                 size_t capacity,
                                 const char *source) {
    size_t index;

    if ((destination == NULL) || (capacity == 0U) || (source == NULL)) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            return true;
        }
    }
    destination[capacity - 1U] = '\0';
    return false;
}

static bool syscall_system_hardware(struct boring_system_info *info) {
    const struct boring_cpu_inventory *cpu;
    const struct boring_platform_identity *platform;
    const struct boring_pci_inventory *pci;
    const struct boring_framebuffer *framebuffer;
    const struct block_device *storage;
    struct virtio_blk_stats storage_stats;
    size_t index;

    if (info == NULL) {
        return false;
    }
    cpu = boring_cpu_inventory_get();
    if ((cpu != NULL) && cpu->leaf1_valid && (cpu->vendor[0] != '\0') &&
        syscall_copy_literal(info->cpu_vendor, sizeof(info->cpu_vendor),
                             cpu->vendor) &&
        syscall_copy_literal(info->cpu_brand, sizeof(info->cpu_brand),
                             cpu->brand)) {
        info->hardware_flags |= BORING_SYSTEM_HW_CPU;
        info->cpu_family = cpu->family;
        info->cpu_model = cpu->model;
        info->cpu_stepping = cpu->stepping;
    }

    platform = boring_platform_identity_get();
    if ((platform != NULL) && platform->available) {
        if (((platform->system_manufacturer[0] != '\0') ||
             (platform->system_product[0] != '\0')) &&
            syscall_copy_literal(info->system_manufacturer,
                                 sizeof(info->system_manufacturer),
                                 platform->system_manufacturer) &&
            syscall_copy_literal(info->system_product,
                                 sizeof(info->system_product),
                                 platform->system_product)) {
            info->hardware_flags |= BORING_SYSTEM_HW_SYSTEM;
        }
        if (((platform->board_manufacturer[0] != '\0') ||
             (platform->board_product[0] != '\0')) &&
            syscall_copy_literal(info->board_manufacturer,
                                 sizeof(info->board_manufacturer),
                                 platform->board_manufacturer) &&
            syscall_copy_literal(info->board_product,
                                 sizeof(info->board_product),
                                 platform->board_product)) {
            info->hardware_flags |= BORING_SYSTEM_HW_BOARD;
        }
        if (((platform->firmware_vendor[0] != '\0') ||
             (platform->firmware_version[0] != '\0')) &&
            syscall_copy_literal(info->firmware_vendor,
                                 sizeof(info->firmware_vendor),
                                 platform->firmware_vendor) &&
            syscall_copy_literal(info->firmware_version,
                                 sizeof(info->firmware_version),
                                 platform->firmware_version)) {
            info->hardware_flags |= BORING_SYSTEM_HW_FIRMWARE;
        }
        if (platform->memory_info_available) {
            info->hardware_flags |= BORING_SYSTEM_HW_SMBIOS_MEMORY;
            if (platform->memory_size_complete) {
                info->hardware_flags |=
                    BORING_SYSTEM_HW_SMBIOS_MEMORY_COMPLETE;
            }
            info->smbios_memory_bytes = platform->memory_bytes;
            info->smbios_memory_slots = platform->memory_device_slots;
            info->smbios_memory_devices_present =
                platform->memory_devices_present;
        }
    }

    pci = boring_pci_inventory_get();
    if ((pci != NULL) && (pci->complete || (pci->total != 0U))) {
        info->hardware_flags |= BORING_SYSTEM_HW_PCI;
        if (pci->complete && !pci->truncated) {
            info->hardware_flags |= BORING_SYSTEM_HW_PCI_COMPLETE;
        }
        info->pci_device_count = pci->total;
        info->pci_sample_count = pci->stored;
        if (info->pci_sample_count > BORING_SYSTEM_PCI_SAMPLE_MAX) {
            info->pci_sample_count = BORING_SYSTEM_PCI_SAMPLE_MAX;
        }
        for (index = 0U; index < (size_t)info->pci_sample_count; ++index) {
            const struct boring_pci_entry *source = &pci->entries[index];
            struct boring_system_pci_sample *destination =
                &info->pci_samples[index];

            destination->vendor_id = source->vendor_id;
            destination->device_id = source->device_id;
            destination->bus = source->bdf.bus;
            destination->device = source->bdf.device;
            destination->function = source->bdf.function;
            destination->class_code = source->class_code;
            destination->subclass = source->subclass;
            destination->prog_if = source->prog_if;
            destination->revision = source->revision;
        }
    }

    framebuffer = boring_framebuffer_get();
    if ((framebuffer != NULL) &&
        (framebuffer->width <= UINT32_MAX) &&
        (framebuffer->height <= UINT32_MAX) &&
        (framebuffer->pitch <= UINT32_MAX)) {
        info->hardware_flags |= BORING_SYSTEM_HW_FRAMEBUFFER;
        info->framebuffer_width = (uint32_t)framebuffer->width;
        info->framebuffer_height = (uint32_t)framebuffer->height;
        info->framebuffer_pitch = (uint32_t)framebuffer->pitch;
        info->framebuffer_bpp = framebuffer->bpp;
    }

    storage = virtio_blk_device();
    if ((storage != NULL) && (storage->name != NULL) &&
        (storage->logical_block_size != 0U) &&
        (storage->block_count <=
         (UINT64_MAX / (uint64_t)storage->logical_block_size)) &&
        syscall_copy_literal(info->storage_name,
                             sizeof(info->storage_name), storage->name)) {
        info->hardware_flags |= BORING_SYSTEM_HW_STORAGE;
        info->storage_logical_block_size = storage->logical_block_size;
        info->storage_bytes = storage->block_count *
            (uint64_t)storage->logical_block_size;
        info->storage_read_only = storage->read_only ? 1U : 0U;
        if (virtio_blk_get_stats(&storage_stats) &&
            storage_stats.pci_discovered) {
            info->hardware_flags |= BORING_SYSTEM_HW_STORAGE_PCI;
            info->storage_pci_vendor_id = storage_stats.pci_vendor_id;
            info->storage_pci_device_id = storage_stats.pci_device_id;
            info->storage_pci_bus = storage_stats.pci_bus;
            info->storage_pci_device = storage_stats.pci_device;
            info->storage_pci_function = storage_stats.pci_function;
        }
    }
    return true;
}

static size_t syscall_text_length(const char *text, size_t maximum) {
    size_t length;

    if (text == NULL) {
        return maximum + 1U;
    }
    for (length = 0U; length <= maximum; ++length) {
        if (text[length] == '\0') {
            return length;
        }
    }
    return maximum + 1U;
}

static uint64_t syscall_system_info(uint64_t user_info) {
    struct pmm_stats memory_stats;
    struct process_stats process_stats;
    struct timer_stats timer_stats;
    struct process *const process = process_current();
    struct boring_system_info info;

    if (!syscall_user_range_accessible((uintptr_t)user_info, sizeof(info),
                                       true)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    if ((process == NULL) || !process_is_alive(process) ||
        !pmm_get_stats(&memory_stats) ||
        !process_get_stats(&process_stats) ||
        (memory_stats.free_frames > UINT64_MAX / PMM_PAGE_SIZE) ||
        (process_stats.active_processes > UINT32_MAX)) {
        return syscall_error(BORING_SYSCALL_EIO);
    }

    syscall_zero_bytes(&info, sizeof(info));
    info.abi_version = BORING_SYSTEM_INFO_ABI_VERSION;
    info.usable_memory_bytes = memory_stats.usable_bytes;
    info.free_memory_bytes = memory_stats.free_frames * PMM_PAGE_SIZE;
    info.process_count = (uint32_t)process_stats.active_processes;
    info.current_pid = process->pid;
    if (timer_get_stats(&timer_stats)) {
        info.uptime_ticks = timer_ticks();
        info.timer_frequency_millihz = timer_stats.effective_frequency_millihz;
    }
    if (!syscall_copy_literal(info.hostname, sizeof(info.hostname), "boringos") ||
        !syscall_copy_literal(info.username, sizeof(info.username),
                              process->username) ||
        !syscall_copy_literal(info.os_name, sizeof(info.os_name), "BoringOS") ||
        !syscall_copy_literal(info.kernel_name, sizeof(info.kernel_name),
                              "BoringKernel") ||
        !syscall_copy_literal(info.kernel_version, sizeof(info.kernel_version),
                              "0.0.62-dev") ||
        !syscall_copy_literal(info.arch, sizeof(info.arch), "x86_64")) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if (!syscall_system_hardware(&info)) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
#if (BORING_TEST_MODE == 10) || (BORING_TEST_MODE == 13) || (BORING_TEST_MODE == 14)
    if (!syscall_copy_literal(info.root_fs, sizeof(info.root_fs), "RAMFS") ||
        !syscall_copy_literal(info.root_device, sizeof(info.root_device),
                              "memory")) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
#elif (BORING_TEST_MODE == 15) || defined(BORING_M36_DESKTOP_ACCEPTANCE)
    if (!syscall_copy_literal(info.root_fs, sizeof(info.root_fs), "BoringFS") ||
        !syscall_copy_literal(info.root_device, sizeof(info.root_device),
#if defined(BORING_M57_AHCI_ROOT)
                              "ahci-sata0")) {
#else
                              "virtio-blk")) {
#endif
        return syscall_error(BORING_SYSCALL_EIO);
    }
#endif
    if (!syscall_copy_to_user((uintptr_t)user_info, &info, sizeof(info))) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    return 0ULL;
}

static void bootstrap_program_clear(void) {
    size_t index;

    for (index = 0U; index <= (size_t)BORING_SYSCALL_LAUNCH_NAME_MAX; ++index) {
        bootstrap_program.name[index] = '\0';
    }
    bootstrap_program.name_length = 0U;
    bootstrap_program.module_bytes = NULL;
    bootstrap_program.module_size = 0U;
    bootstrap_program.registered = false;
}

static void suspended_launch_slot_clear(
    struct syscall_suspended_launch *launch) {
    if (launch == NULL) {
        return;
    }
    launch->parent = NULL;
    launch->child = NULL;
    syscall_zero_bytes(&launch->parent_frame, sizeof(launch->parent_frame));
    syscall_zero_bytes(&launch->image, sizeof(launch->image));
    launch->exit_status = 0;
    launch->active = false;
    launch->child_exited = false;
    launch->image_loaded = false;
}

static void suspended_launch_clear(void) {
    size_t index;

    for (index = 0U; index < (size_t)SYSCALL_LAUNCH_DEPTH_MAX; ++index) {
        suspended_launch_slot_clear(&suspended_launch[index]);
    }
    suspended_launch_depth = 0U;
}

static struct syscall_suspended_launch *suspended_launch_top(void) {
    if (suspended_launch_depth == 0U) {
        return NULL;
    }
    return &suspended_launch[suspended_launch_depth - 1U];
}

bool syscall_stack_contains(uintptr_t stack_pointer) {
    const uintptr_t base = (uintptr_t)&x86_64_syscall_stack[0];
    const uintptr_t top = base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;

    return ((stack_pointer >= base) && (stack_pointer < top)) ||
           task_current_stack_contains((const void *)stack_pointer);
}

static bool syscall_frame_on_trusted_stack(uintptr_t frame_address) {
    uintptr_t last;

    if (frame_address > UINTPTR_MAX - (uintptr_t)(sizeof(struct x86_64_syscall_frame) - 1U)) {
        return false;
    }
    last = frame_address +
           (uintptr_t)(sizeof(struct x86_64_syscall_frame) - 1U);
    return syscall_stack_contains(frame_address) && syscall_stack_contains(last);
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

static bool syscall_copy_to_process(struct process *process,
                                    uintptr_t user_address,
                                    const void *source_buffer,
                                    size_t length) {
    struct vmm_stats vmm_stats;
    const uint8_t *input = (const uint8_t *)source_buffer;
    uintptr_t current = user_address;
    size_t remaining = length;

    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap || (source_buffer == NULL) ||
        !ring3_user_range_valid(user_address, length) ||
        !vmm_get_stats(&vmm_stats)) {
        return false;
    }
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
    if (!syscall_copy_from_user(buffer, (uintptr_t)user_address, safe_length)) {
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

static int syscall_copy_launch_arguments(
    uint64_t user_argv,
    uint64_t raw_argc,
    struct syscall_launch_arguments *arguments) {
    uint64_t pointers[BORING_SYSCALL_ARG_MAX];
    size_t argc;
    size_t index;

    if ((arguments == NULL) || (raw_argc == 0ULL) ||
        (raw_argc > (uint64_t)BORING_SYSCALL_ARG_MAX) ||
        (raw_argc > (uint64_t)SIZE_MAX)) {
        return BORING_SYSCALL_EINVAL;
    }
    argc = (size_t)raw_argc;
    syscall_zero_bytes(arguments, sizeof(*arguments));
    if (!syscall_copy_from_user(pointers, (uintptr_t)user_argv,
                                argc * sizeof(pointers[0]))) {
        return BORING_SYSCALL_EFAULT;
    }
    arguments->argc = argc;
    for (index = 0U; index < argc; ++index) {
        size_t argument_length = 0U;
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
            if ((uint64_t)argument_length > UINT64_MAX - pointer) {
                return BORING_SYSCALL_EFAULT;
            }
            address = pointer + (uint64_t)argument_length;
            if (!syscall_copy_from_user(&value, (uintptr_t)address, 1U)) {
                return BORING_SYSCALL_EFAULT;
            }
            arguments->data[arguments->total_bytes] = value;
            ++arguments->total_bytes;
            if (value == '\0') {
                break;
            }
            ++argument_length;
        }
    }
    return 0;
}

static void syscall_store_u64(uint8_t *destination, uint64_t value) {
    size_t index;

    for (index = 0U; index < sizeof(value); ++index) {
        destination[index] = (uint8_t)(value >> (index * 8U));
    }
}

static bool syscall_prepare_child_arguments(
    struct process *child,
    const struct boring_elf_image *image,
    const struct syscall_launch_arguments *arguments,
    uintptr_t *entry_rsp_out,
    uintptr_t *argv_out) {
    uint8_t block[SYSCALL_ARG_BLOCK_MAX];
    size_t pointer_bytes;
    size_t block_size;
    size_t aligned_size;
    uintptr_t entry_rsp;
    size_t index;

    if ((child == NULL) || (image == NULL) || (arguments == NULL) ||
        (entry_rsp_out == NULL) || (argv_out == NULL) ||
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
    if ((aligned_size > BOOTSTRAP_PROGRAM_STACK_SIZE) ||
        ((size_t)SYSCALL_ARG_RUNTIME_RESERVE >
         BOOTSTRAP_PROGRAM_STACK_SIZE - aligned_size)) {
        return false;
    }
    entry_rsp = image->stack_top - (uintptr_t)aligned_size;
    if ((entry_rsp & 0x0fU) != 0U) {
        return false;
    }
    syscall_zero_bytes(block, sizeof(block));
    for (index = 0U; index < arguments->argc; ++index) {
        const uintptr_t string_address =
            entry_rsp + (uintptr_t)pointer_bytes +
            (uintptr_t)arguments->offsets[index];
        syscall_store_u64(&block[index * sizeof(uint64_t)],
                          (uint64_t)string_address);
    }
    for (index = 0U; index < arguments->total_bytes; ++index) {
        block[pointer_bytes + index] = (uint8_t)arguments->data[index];
    }
    if (!syscall_copy_to_process(child, entry_rsp, block, block_size)) {
        return false;
    }
    *entry_rsp_out = entry_rsp;
    *argv_out = entry_rsp;
    return true;
}

static const char *syscall_path_basename(const char *path) {
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

static int syscall_vfs_error(enum vfs_result result) {
    switch (result) {
        case VFS_RESULT_OK:
            return 0;
        case VFS_RESULT_NOT_FOUND:
            return BORING_SYSCALL_ENOENT;
        case VFS_RESULT_ALREADY_EXISTS:
            return BORING_SYSCALL_EEXIST;
        case VFS_RESULT_NOT_DIRECTORY:
            return BORING_SYSCALL_ENOTDIR;
        case VFS_RESULT_NOT_REGULAR:
            return BORING_SYSCALL_EISDIR;
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
    if (!syscall_copy_to_user((uintptr_t)user_buffer, &buffer[0], safe_length)) {
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
                               uint64_t user_path,
                               uint64_t path_length,
                               uint64_t user_argv,
                               uint64_t raw_argc) {
    char path[BORING_SYSCALL_EXEC_PATH_MAX + 1U];
    struct syscall_launch_arguments arguments;
    struct process *const parent = process_current();
    struct process *child = NULL;
    struct vfs_path parent_cwd = { NULL, NULL };
    struct boring_elf_image image;
    struct boring_elf_memory_source memory_source;
    struct boring_elf_vfs_source vfs_source = {
        { 0ULL, NULL, NULL }, { { NULL, NULL }, 0ULL, 0U, false }, false
    };
    const struct boring_elf_source *source = NULL;
    struct ring3_user_mapping_info entry_info;
    struct ring3_user_mapping_info stack_info;
    struct syscall_suspended_launch *launch;
    const char *process_name;
    enum vfs_result vfs_result;
    uintptr_t entry_rsp = 0U;
    uintptr_t child_argv = 0U;
    int copy_error;
    bool source_is_vfs = false;
    bool image_loaded = false;

    if ((frame == NULL) || (parent == NULL) || !process_is_alive(parent) ||
        (parent->pid == 0ULL) ||
        (suspended_launch_depth >= (size_t)SYSCALL_LAUNCH_DEPTH_MAX)) {
        return syscall_error(BORING_SYSCALL_EACCES);
    }
    if (suspended_launch_depth == 0U) {
        if (parent->pid != 1ULL) {
            return syscall_error(BORING_SYSCALL_EACCES);
        }
    } else {
        struct syscall_suspended_launch *const current = suspended_launch_top();

        if ((current == NULL) || !current->active || current->child_exited ||
            (current->child != parent)) {
            return syscall_error(BORING_SYSCALL_EACCES);
        }
    }
    copy_error = syscall_copy_explicit_string(
        user_path, path_length, (size_t)BORING_SYSCALL_EXEC_PATH_MAX, path);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    copy_error = syscall_copy_launch_arguments(user_argv, raw_argc, &arguments);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }

    if ((parent->pid == 1ULL) && bootstrap_program.registered &&
        bootstrap_program_name_equals(path, (size_t)path_length)) {
        if (!boring_elf_memory_source_init(&memory_source,
                                           bootstrap_program.module_bytes,
                                           bootstrap_program.module_size)) {
            return syscall_error(BORING_SYSCALL_EIO);
        }
        source = &memory_source.source;
    } else {
        vfs_result = boring_elf_vfs_source_open(parent, path, &vfs_source);
        if (vfs_result != VFS_RESULT_OK) {
            return syscall_error((vfs_result == VFS_RESULT_OVERFLOW) ?
                BORING_SYSCALL_ENOEXEC : syscall_vfs_error(vfs_result));
        }
        source = &vfs_source.source;
        source_is_vfs = true;
    }

    process_name = syscall_path_basename(path);
    if ((process_name == NULL) || (process_name[0] == '\0') ||
        (syscall_text_length(process_name, (size_t)KERNEL_PROCESS_NAME_MAX) >
         (size_t)KERNEL_PROCESS_NAME_MAX)) {
        if (source_is_vfs) {
            (void)boring_elf_vfs_source_close(&vfs_source);
        }
        return syscall_error(BORING_SYSCALL_ENAMETOOLONG);
    }
    if (!process_get_cwd(parent, &parent_cwd)) {
        if (source_is_vfs) {
            (void)boring_elf_vfs_source_close(&vfs_source);
        }
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    if (!process_create(&child) || (child == NULL)) {
        (void)vfs_path_release(&parent_cwd);
        if (source_is_vfs) {
            (void)boring_elf_vfs_source_close(&vfs_source);
        }
        return syscall_error(BORING_SYSCALL_ENOSPC);
    }
    if (!process_set_name(child, process_name) ||
        !process_set_cwd(child, &parent_cwd)) {
        (void)vfs_path_release(&parent_cwd);
        if (source_is_vfs) {
            (void)boring_elf_vfs_source_close(&vfs_source);
        }
        return syscall_launch_rollback(child, &image, false,
                                       BORING_SYSCALL_EIO);
    }
    if (vfs_path_release(&parent_cwd) != VFS_RESULT_OK) {
        if (source_is_vfs) {
            (void)boring_elf_vfs_source_close(&vfs_source);
        }
        return syscall_launch_rollback(child, &image, false,
                                       BORING_SYSCALL_EIO);
    }
    if (!boring_elf_load_source(child, source,
                                (uintptr_t)BOOTSTRAP_PROGRAM_STACK_BASE,
                                BOOTSTRAP_PROGRAM_STACK_SIZE, &image)) {
        if (source_is_vfs) {
            (void)boring_elf_vfs_source_close(&vfs_source);
        }
        return syscall_launch_rollback(child, &image, false,
                                       BORING_SYSCALL_ENOEXEC);
    }
    image_loaded = true;
    if (source_is_vfs &&
        (boring_elf_vfs_source_close(&vfs_source) != VFS_RESULT_OK)) {
        return syscall_launch_rollback(child, &image, image_loaded,
                                       BORING_SYSCALL_EIO);
    }
    if (!syscall_prepare_child_arguments(child, &image, &arguments,
                                         &entry_rsp, &child_argv) ||
        (child->address_space.root_physical ==
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
    if (source_is_vfs) {
        serial_write_string("boring-launch: entry executable\n");
    } else {
        serial_write_string("boring-launch: shell entry executable\n");
    }
    if (source_is_vfs) {
        serial_write_string("boring-launch: stack rw-nx\n");
    } else {
        serial_write_string("boring-launch: shell stack rw-nx\n");
    }
    serial_write_string("boring-launch: higher-half supervisor-only\n");
    serial_write_string("boring-launch: cwd inherited\n");
    if (!source_is_vfs) {
        serial_write_string("boring-launch: pid 1 remains alive\n");
    }
    if (source_is_vfs) {
        serial_write_string("boring-launch: VFS executable source ");
        serial_write_string(path);
        serial_write_string("\n");
    } else {
        serial_write_string("boring-launch: boot-module executable source\n");
    }

    launch = &suspended_launch[suspended_launch_depth];
    suspended_launch_slot_clear(launch);
    launch->parent = parent;
    launch->child = child;
    launch->parent_frame = *frame;
    launch->image = image;
    launch->exit_status = 0;
    launch->active = true;
    launch->child_exited = false;
    launch->image_loaded = true;
    ++suspended_launch_depth;

    frame->user_rsp = (uint64_t)entry_rsp;
    frame->user_rip = (uint64_t)image.entry;
    frame->rbx = 0ULL;
    frame->rbp = 0ULL;
    frame->r12 = 0ULL;
    frame->r13 = 0ULL;
    frame->r14 = 0ULL;
    frame->r15 = 0ULL;
    frame->rdi = (uint64_t)arguments.argc;
    frame->rsi = (uint64_t)child_argv;
    frame->rdx = 0ULL;
    frame->r10 = 0ULL;
    frame->r8 = 0ULL;
    frame->r9 = 0ULL;
    frame->reserved = 0ULL;

    if (!process_activate(child)) {
        struct x86_64_syscall_frame saved_parent_frame = launch->parent_frame;

        --suspended_launch_depth;
        suspended_launch_slot_clear(launch);
        *frame = saved_parent_frame;
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
    return (result == VFS_RESULT_OK) ?
        0ULL : syscall_error(syscall_vfs_error(result));
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
    return (result == VFS_RESULT_OK) ?
        0ULL : syscall_error(syscall_vfs_error(result));
}

static bool syscall_cwd_pop(char *output, size_t *length) {
    if ((output == NULL) || (length == NULL) || (*length == 0U) ||
        (output[0] != '/')) {
        return false;
    }
    if (*length <= 1U) {
        *length = 1U;
        output[1] = '\0';
        return true;
    }
    while ((*length > 1U) && (output[*length - 1U] != '/')) {
        --(*length);
    }
    if (*length > 1U) {
        --(*length);
    }
    output[*length] = '\0';
    return true;
}

static bool syscall_cwd_append(char *output,
                               size_t *length,
                               const char *component,
                               size_t component_length) {
    size_t index;

    if ((output == NULL) || (length == NULL) || (component == NULL) ||
        (component_length == 0U) ||
        (component_length > (size_t)VFS_NAME_MAX)) {
        return false;
    }
    if ((*length > 1U) && (*length >= (size_t)VFS_PATH_MAX)) {
        return false;
    }
    if (*length > 1U) {
        output[*length] = '/';
        ++(*length);
    }
    if (component_length > (size_t)VFS_PATH_MAX - *length) {
        return false;
    }
    for (index = 0U; index < component_length; ++index) {
        output[*length + index] = component[index];
    }
    *length += component_length;
    output[*length] = '\0';
    return true;
}

static bool syscall_build_canonical_cwd(struct process *process,
                                        const char *path,
                                        char output[VFS_PATH_MAX + 1U]) {
    size_t output_length;
    size_t index;

    if ((process == NULL) || (path == NULL) || (output == NULL)) {
        return false;
    }
    if (path[0] == '/') {
        output[0] = '/';
        output[1] = '\0';
        output_length = 1U;
        index = 1U;
    } else {
        if (!process_get_cwd_text(process, output, (size_t)VFS_PATH_MAX + 1U)) {
            return false;
        }
        output_length = syscall_text_length(output, (size_t)VFS_PATH_MAX);
        if ((output_length == 0U) || (output_length > (size_t)VFS_PATH_MAX)) {
            return false;
        }
        index = 0U;
    }

    while (path[index] != '\0') {
        size_t start;
        size_t component_length;

        while (path[index] == '/') {
            ++index;
        }
        if (path[index] == '\0') {
            break;
        }
        start = index;
        while ((path[index] != '\0') && (path[index] != '/')) {
            ++index;
        }
        component_length = index - start;
        if ((component_length == 1U) && (path[start] == '.')) {
            continue;
        }
        if ((component_length == 2U) && (path[start] == '.') &&
            (path[start + 1U] == '.')) {
            if (!syscall_cwd_pop(output, &output_length)) {
                return false;
            }
            continue;
        }
        if (!syscall_cwd_append(output, &output_length, &path[start],
                                component_length)) {
            return false;
        }
    }
    return true;
}

static uint64_t syscall_fs_chdir(uint64_t user_path, uint64_t length) {
    char path[VFS_PATH_MAX + 1U];
    char canonical[VFS_PATH_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path resolved = { NULL, NULL };
    enum vfs_result result;
    int copy_error;

    copy_error = syscall_copy_explicit_string(user_path, length,
                                              (size_t)VFS_PATH_MAX, path);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    if ((process == NULL) || !process_is_alive(process) ||
        !syscall_build_canonical_cwd(process, path, canonical)) {
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
    if (!process_set_cwd_text(process, &resolved, canonical)) {
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
    for (name_index = 0U; name_index < kernel_entry.name_length; ++name_index) {
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

static enum vfs_result syscall_fs_resolve_parent(
    struct process *process,
    char *path,
    size_t path_length,
    struct vfs_path *parent_out,
    char name_out[VFS_NAME_MAX + 1U]) {
    size_t slash = SIZE_MAX;
    size_t name_start;
    size_t name_length;
    size_t index;

    if ((process == NULL) || !process_is_alive(process) || (path == NULL) ||
        (path_length == 0U) || (parent_out == NULL) || (name_out == NULL) ||
        (parent_out->mount != NULL) || (parent_out->node != NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    for (index = 0U; index < path_length; ++index) {
        if (path[index] == '/') {
            slash = index;
        }
    }
    name_start = (slash == SIZE_MAX) ? 0U : slash + 1U;
    name_length = path_length - name_start;
    if (name_length == 0U) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    if (name_length > (size_t)VFS_NAME_MAX) {
        return VFS_RESULT_NAME_TOO_LONG;
    }
    for (index = 0U; index < name_length; ++index) {
        name_out[index] = path[name_start + index];
    }
    name_out[name_length] = '\0';
    if (slash == SIZE_MAX) {
        return process_get_cwd(process, parent_out) ?
            VFS_RESULT_OK : VFS_RESULT_NO_CWD;
    }
    if (slash == 0U) {
        path[1] = '\0';
    } else {
        path[slash] = '\0';
    }
    return vfs_resolve_process(process, path, parent_out);
}

static uint64_t syscall_fs_read(uint64_t user_path,
                                uint64_t path_length,
                                uint64_t offset,
                                uint64_t user_buffer,
                                uint64_t capacity) {
    char path[VFS_PATH_MAX + 1U];
    uint8_t buffer[BORING_SYSCALL_FS_IO_MAX];
    struct process *const process = process_current();
    struct vfs_path resolved = { NULL, NULL };
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    size_t safe_capacity;
    size_t transferred = 0U;
    enum vfs_result result;
    int copy_error;

    if ((capacity == 0ULL) ||
        (capacity > (uint64_t)BORING_SYSCALL_FS_IO_MAX) ||
        (capacity > (uint64_t)SIZE_MAX)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    safe_capacity = (size_t)capacity;
    if (!syscall_user_range_accessible((uintptr_t)user_buffer,
                                       safe_capacity, true)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    copy_error = syscall_copy_explicit_string(
        user_path, path_length, (size_t)VFS_PATH_MAX, path);
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
    result = vfs_handle_open(&resolved, VFS_ACCESS_READ, &handle);
    if (result != VFS_RESULT_OK) {
        (void)vfs_path_release(&resolved);
        return syscall_error(syscall_vfs_error(result));
    }
    if (vfs_path_release(&resolved) != VFS_RESULT_OK) {
        (void)vfs_handle_close(&handle);
        return syscall_error(BORING_SYSCALL_EIO);
    }
    handle.offset = offset;
    result = vfs_handle_read(&handle, buffer, safe_capacity, &transferred);
    if (vfs_handle_close(&handle) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    if ((transferred != 0U) &&
        !syscall_copy_to_user((uintptr_t)user_buffer, buffer, transferred)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    return (uint64_t)transferred;
}

static uint64_t syscall_fs_touch(uint64_t user_path, uint64_t path_length) {
    char path[VFS_PATH_MAX + 1U];
    char name[VFS_NAME_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path parent = { NULL, NULL };
    struct vfs_path created = { NULL, NULL };
    enum vfs_result result;
    int copy_error;

    copy_error = syscall_copy_explicit_string(
        user_path, path_length, (size_t)VFS_PATH_MAX, path);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    result = syscall_fs_resolve_parent(process, path, (size_t)path_length,
                                       &parent, name);
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    result = vfs_create_at(&parent, name, &created);
    if (result == VFS_RESULT_OK) {
        if (vfs_path_release(&created) != VFS_RESULT_OK) {
            (void)vfs_path_release(&parent);
            return syscall_error(BORING_SYSCALL_EIO);
        }
    }
    if (vfs_path_release(&parent) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    return (result == VFS_RESULT_OK) ?
        0ULL : syscall_error(syscall_vfs_error(result));
}

static uint64_t syscall_fs_write(uint64_t user_path,
                                 uint64_t path_length,
                                 uint64_t user_buffer,
                                 uint64_t length) {
    char path[VFS_PATH_MAX + 1U];
    uint8_t buffer[BORING_SYSCALL_FS_IO_MAX];
    struct process *const process = process_current();
    struct vfs_path resolved = { NULL, NULL };
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    size_t safe_length;
    size_t transferred = 0U;
    enum vfs_result result;
    int copy_error;

    if ((length > (uint64_t)BORING_SYSCALL_FS_IO_MAX) ||
        (length > (uint64_t)SIZE_MAX)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    safe_length = (size_t)length;
    if ((safe_length != 0U) &&
        !syscall_copy_from_user(buffer, (uintptr_t)user_buffer, safe_length)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    copy_error = syscall_copy_explicit_string(
        user_path, path_length, (size_t)VFS_PATH_MAX, path);
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
    result = vfs_truncate_path(&resolved, 0ULL);
    if (result != VFS_RESULT_OK) {
        (void)vfs_path_release(&resolved);
        return syscall_error(syscall_vfs_error(result));
    }
    if (safe_length == 0U) {
        if (vfs_path_release(&resolved) != VFS_RESULT_OK) {
            return syscall_error(BORING_SYSCALL_EIO);
        }
        return 0ULL;
    }
    result = vfs_handle_open(&resolved, VFS_ACCESS_WRITE, &handle);
    if (result != VFS_RESULT_OK) {
        (void)vfs_path_release(&resolved);
        return syscall_error(syscall_vfs_error(result));
    }
    if (vfs_path_release(&resolved) != VFS_RESULT_OK) {
        (void)vfs_handle_close(&handle);
        return syscall_error(BORING_SYSCALL_EIO);
    }
    result = vfs_handle_write(&handle, buffer, safe_length, &transferred);
    if (vfs_handle_close(&handle) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    if (transferred != safe_length) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    return (uint64_t)transferred;
}

static uint64_t syscall_fs_unlink(uint64_t user_path,
                                  uint64_t path_length) {
    char path[VFS_PATH_MAX + 1U];
    char name[VFS_NAME_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path parent = { NULL, NULL };
    enum vfs_result result;
    int copy_error;

    copy_error = syscall_copy_explicit_string(
        user_path, path_length, (size_t)VFS_PATH_MAX, path);
    if (copy_error != 0) {
        return syscall_error(copy_error);
    }
    result = syscall_fs_resolve_parent(process, path, (size_t)path_length,
                                       &parent, name);
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    result = vfs_unlink_at(&parent, name);
    if (vfs_path_release(&parent) != VFS_RESULT_OK) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    return (result == VFS_RESULT_OK) ?
        0ULL : syscall_error(syscall_vfs_error(result));
}

static uint64_t syscall_fd_open(uint64_t user_path,
                                uint64_t path_length,
                                uint64_t raw_flags) {
    char path[VFS_PATH_MAX + 1U];
    struct process *const process = process_current();
    struct vfs_path resolved = { NULL, NULL };
    uint32_t flags;
    uint32_t access = 0U;
    uint32_t fd = 0U;
    enum vfs_result result;
    int copy_error;

    if ((raw_flags == 0ULL) ||
        (raw_flags > (uint64_t)UINT32_MAX) ||
        ((raw_flags & ~(uint64_t)BORING_FD_OPEN_MASK) != 0ULL)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    flags = (uint32_t)raw_flags;
    if ((flags & BORING_FD_OPEN_READ) != 0U) {
        access |= VFS_ACCESS_READ;
    }
    if ((flags & BORING_FD_OPEN_WRITE) != 0U) {
        access |= VFS_ACCESS_WRITE;
    }
    copy_error = syscall_copy_explicit_string(
        user_path, path_length, (size_t)VFS_PATH_MAX, path);
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
    result = kernel_fd_open_regular(&process->fd_table, &resolved, access, &fd);
    if (result != VFS_RESULT_OK) {
        (void)vfs_path_release(&resolved);
        return syscall_error(syscall_vfs_error(result));
    }
    if (vfs_path_release(&resolved) != VFS_RESULT_OK) {
        (void)kernel_fd_close(&process->fd_table, fd);
        return syscall_error(BORING_SYSCALL_EIO);
    }
    serial_write_string("fd-open: pid ");
    serial_write_u64(process->pid);
    serial_write_string(" path ");
    serial_write_string(path);
    serial_write_string(" fd ");
    serial_write_u64((uint64_t)fd);
    serial_write_string("\n");
    return (uint64_t)fd;
}

static uint64_t syscall_fd_read(uint64_t raw_fd,
                                uint64_t user_buffer,
                                uint64_t capacity) {
    uint8_t buffer[BORING_SYSCALL_FD_IO_MAX];
    struct process *const process = process_current();
    enum kernel_fd_kind kind;
    uint32_t access;
    uint32_t fd;
    size_t safe_capacity;
    size_t transferred = 0U;
    size_t index;
    enum vfs_result result;

    if ((raw_fd > (uint64_t)UINT32_MAX) ||
        (capacity > (uint64_t)BORING_SYSCALL_FD_IO_MAX) ||
        (capacity > (uint64_t)SIZE_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    fd = (uint32_t)raw_fd;
    if (!kernel_fd_describe(&process->fd_table, fd, &kind, &access)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    if ((access & VFS_ACCESS_READ) == 0U) {
        return syscall_error(BORING_SYSCALL_EACCES);
    }
    safe_capacity = (size_t)capacity;
    if (safe_capacity == 0U) {
        return 0ULL;
    }
    if (!syscall_user_range_accessible((uintptr_t)user_buffer,
                                       safe_capacity, true)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    if (kind == KERNEL_FD_CONSOLE_INPUT) {
        for (index = 0U; index < safe_capacity; ++index) {
            buffer[index] = (uint8_t)serial_read_char_blocking();
        }
        transferred = safe_capacity;
    } else if (kind == KERNEL_FD_REGULAR) {
        result = kernel_fd_read_regular(&process->fd_table, fd, buffer,
                                        safe_capacity, &transferred);
        if (result != VFS_RESULT_OK) {
            return syscall_error(syscall_vfs_error(result));
        }
    } else if (kind == KERNEL_FD_PTY) {
        for (;;) {
            enum pty_result pty_result;

            pty_result = kernel_fd_read_pty(&process->fd_table, fd, buffer,
                                            safe_capacity, &transferred);
            if (pty_result == PTY_RESULT_OK) {
                break;
            }
            if (pty_result == PTY_RESULT_HUP) {
                transferred = 0U;
                break;
            }
            if (pty_result != PTY_RESULT_WOULD_BLOCK) {
                return syscall_error(BORING_SYSCALL_EINVAL);
            }
            if (kernel_fd_arm_pty_waiter(&process->fd_table, fd,
                                         process->pid) != PTY_RESULT_OK) {
                return syscall_error(BORING_SYSCALL_EINVAL);
            }
            pty_result = kernel_fd_read_pty(&process->fd_table, fd, buffer,
                                            safe_capacity, &transferred);
            if (pty_result != PTY_RESULT_WOULD_BLOCK) {
                kernel_fd_cancel_pty_waiter(&process->fd_table, fd,
                                            process->pid);
                if (pty_result == PTY_RESULT_OK) {
                    break;
                }
                if (pty_result == PTY_RESULT_HUP) {
                    transferred = 0U;
                    break;
                }
                return syscall_error(BORING_SYSCALL_EINVAL);
            }
            if (!task_block_current()) {
                kernel_fd_cancel_pty_waiter(&process->fd_table, fd,
                                            process->pid);
                return syscall_error(BORING_SYSCALL_EBUSY);
            }
            kernel_fd_cancel_pty_waiter(&process->fd_table, fd, process->pid);
        }
    } else {
        return syscall_error(BORING_SYSCALL_EACCES);
    }
    if ((transferred != 0U) &&
        !syscall_copy_to_user((uintptr_t)user_buffer, buffer, transferred)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    /* Input tracing must not insert diagnostic newlines into the same serial
     * stream that carries the console editor's prompts and ANSI redraws. */
    if (kind == KERNEL_FD_CONSOLE_INPUT) {
        return (uint64_t)transferred;
    }
    serial_write_string("fd-read: pid ");
    serial_write_u64(process->pid);
    serial_write_string(" fd ");
    serial_write_u64((uint64_t)fd);
    if ((kind == KERNEL_FD_REGULAR) && (transferred == 0U)) {
        serial_write_string(" EOF\n");
    } else {
        serial_write_string(" bytes ");
        serial_write_u64((uint64_t)transferred);
        serial_write_string("\n");
    }
    return (uint64_t)transferred;
}

static uint64_t syscall_fd_write(uint64_t raw_fd,
                                 uint64_t user_buffer,
                                 uint64_t length) {
    uint8_t buffer[BORING_SYSCALL_FD_IO_MAX];
    struct process *const process = process_current();
    enum kernel_fd_kind kind;
    uint32_t access;
    uint32_t fd;
    size_t safe_length;
    size_t transferred = 0U;
    enum vfs_result result;

    if ((raw_fd > (uint64_t)UINT32_MAX) ||
        (length > (uint64_t)BORING_SYSCALL_FD_IO_MAX) ||
        (length > (uint64_t)SIZE_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    fd = (uint32_t)raw_fd;
    if (!kernel_fd_describe(&process->fd_table, fd, &kind, &access)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    if ((access & VFS_ACCESS_WRITE) == 0U) {
        return syscall_error(BORING_SYSCALL_EACCES);
    }
    safe_length = (size_t)length;
    if (safe_length == 0U) {
        return 0ULL;
    }
    if (!syscall_copy_from_user(buffer, (uintptr_t)user_buffer, safe_length)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    if (kind == KERNEL_FD_CONSOLE_OUTPUT) {
        transferred = safe_length;
        /* Do not insert diagnostics inside fragmented user lines/prompts.
         * Keep the existing FD witness after writes ending at a line boundary. */
        serial_write_bytes((const char *)buffer, transferred);
        if (buffer[transferred - 1U] != (uint8_t)'\n') {
            return (uint64_t)transferred;
        }
        serial_write_string("fd-write: pid ");
        serial_write_u64(process->pid);
        serial_write_string(" fd ");
        serial_write_u64((uint64_t)fd);
        serial_write_string(" bytes ");
        serial_write_u64((uint64_t)transferred);
        serial_write_string("\n");
        return (uint64_t)transferred;
    }
    if (kind == KERNEL_FD_PTY) {
        const enum pty_result pty_result =
            kernel_fd_write_pty(&process->fd_table, fd, buffer, safe_length,
                                &transferred);

        if (pty_result == PTY_RESULT_HUP) {
            return syscall_error(BORING_SYSCALL_EPIPE);
        }
        if (pty_result == PTY_RESULT_WOULD_BLOCK) {
            return syscall_error(BORING_SYSCALL_EBUSY);
        }
        if (pty_result != PTY_RESULT_OK) {
            return syscall_error(BORING_SYSCALL_EINVAL);
        }
    } else if (kind == KERNEL_FD_REGULAR) {
        result = kernel_fd_write_regular(&process->fd_table, fd, buffer,
                                         safe_length, &transferred);
        if (result != VFS_RESULT_OK) {
            return syscall_error(syscall_vfs_error(result));
        }
    } else {
        return syscall_error(BORING_SYSCALL_EACCES);
    }
    serial_write_string("fd-write: pid ");
    serial_write_u64(process->pid);
    serial_write_string(" fd ");
    serial_write_u64((uint64_t)fd);
    serial_write_string(" bytes ");
    serial_write_u64((uint64_t)transferred);
    serial_write_string("\n");
    return (uint64_t)transferred;
}

static uint64_t syscall_fd_close(uint64_t raw_fd) {
    struct process *const process = process_current();
    uint32_t fd;
    enum vfs_result result;

    if ((raw_fd > (uint64_t)UINT32_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    fd = (uint32_t)raw_fd;
    result = kernel_fd_close(&process->fd_table, fd);
    if (result != VFS_RESULT_OK) {
        return syscall_error(syscall_vfs_error(result));
    }
    serial_write_string("fd-close: pid ");
    serial_write_u64(process->pid);
    serial_write_string(" fd ");
    serial_write_u64((uint64_t)fd);
    serial_write_string("\n");
    return 0ULL;
}

static int syscall_input_error(enum boring_input_result result) {
    switch (result) {
        case BORING_INPUT_RESULT_OK:
            return 0;
        case BORING_INPUT_RESULT_BUSY:
            return BORING_SYSCALL_EBUSY;
        case BORING_INPUT_RESULT_ACCESS:
            return BORING_SYSCALL_EACCES;
        case BORING_INPUT_RESULT_INVALID:
            return BORING_SYSCALL_EINVAL;
        case BORING_INPUT_RESULT_NOT_INITIALIZED:
        default:
            return BORING_SYSCALL_EIO;
    }
}

static uint64_t syscall_input_claim(void) {
    struct process *const process = process_current();
    enum boring_input_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = boring_input_claim(process->pid);
    if (result != BORING_INPUT_RESULT_OK) {
        return syscall_error(syscall_input_error(result));
    }
    serial_write_string("boring-input: claim pid ");
    serial_write_u64(process->pid);
    serial_write_string("\n");
    return 0ULL;
}

static uint64_t syscall_input_read(uint64_t user_events, uint64_t max_events) {
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    struct process *const process = process_current();
    size_t safe_max;
    size_t bytes;
    size_t count;
    enum boring_input_result result;
    bool copied;

    if ((max_events == 0ULL) ||
        (max_events > (uint64_t)BORING_INPUT_READ_MAX) ||
        (max_events > (uint64_t)SIZE_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    safe_max = (size_t)max_events;
    bytes = safe_max * sizeof(struct boring_input_event);
    if (!syscall_user_range_accessible((uintptr_t)user_events, bytes, true)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }

    for (;;) {
        count = 0U;
        x86_64_interrupts_disable();
        result = boring_input_read(process->pid, events, safe_max, &count);
        if (result != BORING_INPUT_RESULT_OK) {
            return syscall_error(syscall_input_error(result));
        }
        if (count != 0U) {
            break;
        }
        if (!boring_input_wait_prepare(process->pid)) {
            return syscall_error(BORING_SYSCALL_EIO);
        }
        /*
         * STI+HLT closes the empty-queue/sleep race on the current single-CPU
         * target: a pending IRQ is delivered before or immediately after HLT.
         * The one trusted SYSCALL stack remains owned by this process.
         */
        x86_64_enable_and_halt();
        x86_64_interrupts_disable();
        boring_input_wait_cancel(process->pid);
    }

    /* Never leave interrupts disabled while copying a userspace buffer. */
    x86_64_interrupts_enable();
    copied = syscall_copy_to_user((uintptr_t)user_events, events,
                                  count * sizeof(struct boring_input_event));
    x86_64_interrupts_disable();
    if (!copied) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    return (uint64_t)count;
}

static uint64_t syscall_input_release(void) {
    struct process *const process = process_current();
    enum boring_input_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = boring_input_release(process->pid);
    if (result != BORING_INPUT_RESULT_OK) {
        return syscall_error(syscall_input_error(result));
    }
    serial_write_string("boring-input: release pid ");
    serial_write_u64(process->pid);
    serial_write_string("\n");
    return 0ULL;
}

static int syscall_user_memory_error(enum user_memory_result result) {
    switch (result) {
        case USER_MEMORY_RESULT_OK:
            return 0;
        case USER_MEMORY_RESULT_INVALID:
            return BORING_SYSCALL_EINVAL;
        case USER_MEMORY_RESULT_NO_SPACE:
            return BORING_SYSCALL_ENOSPC;
        case USER_MEMORY_RESULT_NO_MEMORY:
            return BORING_SYSCALL_ENOMEM;
        case USER_MEMORY_RESULT_INTERNAL:
        case USER_MEMORY_RESULT_NOT_INITIALIZED:
        default:
            return BORING_SYSCALL_EIO;
    }
}

static uint64_t syscall_memory_alloc(uint64_t raw_size) {
    struct process *const process = process_current();
    uintptr_t base = 0U;
    enum user_memory_result result;

    if ((raw_size == 0ULL) || (raw_size > (uint64_t)SIZE_MAX) ||
        (process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_memory_allocate(process, (size_t)raw_size, &base);
    if (result != USER_MEMORY_RESULT_OK) {
        return syscall_error(syscall_user_memory_error(result));
    }
    return (uint64_t)base;
}

static uint64_t syscall_memory_free(uint64_t raw_base) {
    struct process *const process = process_current();
    enum user_memory_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_memory_free(process, (uintptr_t)raw_base);
    return (result == USER_MEMORY_RESULT_OK) ? 0ULL :
        syscall_error(syscall_user_memory_error(result));
}

static uint64_t syscall_buffer_create(uint64_t raw_size) {
    struct process *const process = process_current();
    uint32_t handle = BORING_BUFFER_HANDLE_INVALID;
    enum user_memory_result result;

    if ((raw_size == 0ULL) || (raw_size > (uint64_t)SIZE_MAX) ||
        (process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_create(process, (size_t)raw_size, &handle);
    if (result != USER_MEMORY_RESULT_OK) {
        return syscall_error(syscall_user_memory_error(result));
    }
    return (uint64_t)handle;
}

static uint64_t syscall_buffer_map(uint64_t raw_handle) {
    struct process *const process = process_current();
    uintptr_t base = 0U;
    enum user_memory_result result;

    if ((raw_handle > (uint64_t)UINT32_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_map(process, (uint32_t)raw_handle, &base);
    if (result != USER_MEMORY_RESULT_OK) {
        return syscall_error(syscall_user_memory_error(result));
    }
    return (uint64_t)base;
}

static uint64_t syscall_buffer_unmap(uint64_t raw_base) {
    struct process *const process = process_current();
    enum user_memory_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_unmap(process, (uintptr_t)raw_base);
    return (result == USER_MEMORY_RESULT_OK) ? 0ULL :
        syscall_error(syscall_user_memory_error(result));
}

static uint64_t syscall_buffer_close(uint64_t raw_handle) {
    struct process *const process = process_current();
    enum user_memory_result result;

    if ((raw_handle > (uint64_t)UINT32_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_close(process, (uint32_t)raw_handle);
    return (result == USER_MEMORY_RESULT_OK) ? 0ULL :
        syscall_error(syscall_user_memory_error(result));
}

static uint64_t syscall_getcwd(uint64_t user_buffer, uint64_t capacity) {
    char cwd[VFS_PATH_MAX + 1U];
    struct process *const process = process_current();
    size_t length;

    if ((capacity == 0ULL) || (capacity > (uint64_t)VFS_PATH_MAX + 1ULL) ||
        (process == NULL) || !process_is_alive(process) ||
        !process_get_cwd_text(process, cwd, sizeof(cwd))) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    length = syscall_text_length(cwd, (size_t)VFS_PATH_MAX);
    if ((length > (size_t)VFS_PATH_MAX) ||
        ((uint64_t)(length + 1U) > capacity)) {
        return syscall_error(BORING_SYSCALL_ENAMETOOLONG);
    }
    if (!syscall_user_range_accessible((uintptr_t)user_buffer, length + 1U,
                                       true) ||
        !syscall_copy_to_user((uintptr_t)user_buffer, cwd, length + 1U)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    return (uint64_t)length;
}

static uint64_t syscall_process_snapshot(uint64_t index, uint64_t user_info) {
    struct process_snapshot snapshot;
    struct boring_process_info info;
    size_t name_index;

    if (!syscall_user_range_accessible((uintptr_t)user_info, sizeof(info),
                                       true)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    if (!process_get_snapshot(index, &snapshot)) {
        return 0ULL;
    }
    syscall_zero_bytes(&info, sizeof(info));
    info.pid = snapshot.pid;
    info.parent_pid = snapshot.parent_pid;
    if (snapshot.state == PROCESS_SNAPSHOT_RUNNING) {
        info.state = BORING_PROCESS_STATE_RUNNING;
    } else if (snapshot.state == PROCESS_SNAPSHOT_WAITING) {
        info.state = BORING_PROCESS_STATE_WAITING;
    } else if (snapshot.state == PROCESS_SNAPSHOT_ZOMBIE) {
        info.state = BORING_PROCESS_STATE_ZOMBIE;
    } else {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    for (name_index = 0U; name_index < sizeof(info.name) - 1U; ++name_index) {
        info.name[name_index] = snapshot.name[name_index];
        if (snapshot.name[name_index] == '\0') {
            break;
        }
    }
    info.name[sizeof(info.name) - 1U] = '\0';
    if (!syscall_copy_to_user((uintptr_t)user_info, &info, sizeof(info))) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    return 1ULL;
}

static uint64_t syscall_exit(struct x86_64_syscall_frame *frame,
                             uint64_t raw_status) {
    struct syscall_suspended_launch *const launch = suspended_launch_top();
    struct process *const child = process_current();
    struct process *parent;
    uint64_t child_pid;
    struct user_memory_cleanup_stats memory_cleanup;
    bool input_released = false;

    if ((frame == NULL) || (launch == NULL) || !launch->active ||
        launch->child_exited || (child == NULL) || (child != launch->child) ||
        !process_is_alive(child) || (launch->parent == NULL)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    parent = launch->parent;
    child_pid = child->pid;
    launch->exit_status = (int32_t)raw_status;
    (void)boring_input_process_teardown(child_pid, &input_released);
    if (user_memory_process_cleanup(child, &memory_cleanup) !=
        USER_MEMORY_RESULT_OK) {
        syscall_fatal("cannot release child M32 memory during SYS_EXIT");
    }

    if (!process_activate(parent)) {
        syscall_fatal("cannot resume parent during SYS_EXIT");
    }
    if (launch->image_loaded) {
        if (!boring_elf_unload(&launch->image)) {
            syscall_fatal("cannot release child ELF pages during SYS_EXIT");
        }
        launch->image_loaded = false;
    }
    if (!process_mark_finished(child)) {
        syscall_fatal("cannot mark child zombie during SYS_EXIT");
    }
    launch->child_exited = true;
    serial_write_string("boring-exit: child pid ");
    serial_write_u64(child_pid);
    serial_write_string(" status ");
    serial_write_u64((uint64_t)(uint32_t)launch->exit_status);
    serial_write_string(" is zombie\n");
    if (input_released) {
        serial_write_string("boring-input: owner pid " );
        serial_write_u64(child_pid);
        serial_write_string(" teardown released\n");
    }
    serial_write_string("boring-memory: cleanup pid " );
    serial_write_u64(child_pid);
    serial_write_string(" allocations " );
    serial_write_u64((uint64_t)memory_cleanup.allocations_released);
    serial_write_string(" mappings " );
    serial_write_u64((uint64_t)memory_cleanup.mappings_released);
    serial_write_string(" handles " );
    serial_write_u64((uint64_t)memory_cleanup.handles_released);
    serial_write_string(" objects " );
    serial_write_u64((uint64_t)memory_cleanup.objects_before);
    serial_write_string("->" );
    serial_write_u64((uint64_t)memory_cleanup.objects_after);
    serial_write_string("\n");
    *frame = launch->parent_frame;
    return child_pid;
}

static uint64_t syscall_waitpid(uint64_t pid, uint64_t user_status) {
    struct syscall_suspended_launch *const launch = suspended_launch_top();
    struct process *const parent = process_current();
    struct process *child;
    int32_t status;

    if ((launch == NULL) || (parent == NULL) || !process_is_alive(parent) ||
        !launch->active || !launch->child_exited ||
        (launch->parent != parent) || (launch->child == NULL) ||
        (launch->child->pid != pid) ||
        (launch->child->parent_pid != parent->pid)) {
        return syscall_error(BORING_SYSCALL_ENOENT);
    }
    status = launch->exit_status;
    if ((user_status != 0ULL) &&
        (!syscall_user_range_accessible((uintptr_t)user_status,
                                        sizeof(status), true) ||
         !syscall_copy_to_user((uintptr_t)user_status, &status,
                               sizeof(status)))) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }
    child = launch->child;
    if (!process_destroy(child)) {
        return syscall_error(BORING_SYSCALL_EIO);
    }
    serial_write_string("boring-waitpid: reaped child pid ");
    serial_write_u64(pid);
    serial_write_string("\n");
    suspended_launch_slot_clear(launch);
    --suspended_launch_depth;
    return pid;
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
                              (uintptr_t)frame->user_rip, false, &translated) ||
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
    const uintptr_t stack_top = stack_base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;
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
    suspended_launch_clear();

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
        !syscall_frame_on_trusted_stack(frame_address)) {
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
            result = syscall_launch(frame, frame->rdi, frame->rsi,
                                    frame->rdx, frame->r10);
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
        case BORING_SYS_FS_READ:
            result = syscall_fs_read(frame->rdi, frame->rsi, frame->rdx,
                                     frame->r10, frame->r8);
            break;
        case BORING_SYS_FS_TOUCH:
            result = syscall_fs_touch(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_FS_WRITE:
            result = syscall_fs_write(frame->rdi, frame->rsi, frame->rdx,
                                      frame->r10);
            break;
        case BORING_SYS_FS_UNLINK:
            result = syscall_fs_unlink(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_INFO:
            result = syscall_system_info(frame->rdi);
            break;
        case BORING_SYS_GETCWD:
            result = syscall_getcwd(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_PROCESS_SNAPSHOT:
            result = syscall_process_snapshot(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_EXIT:
            result = syscall_exit(frame, frame->rdi);
            break;
        case BORING_SYS_WAITPID:
            result = syscall_waitpid(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_FD_OPEN:
            result = syscall_fd_open(frame->rdi, frame->rsi, frame->rdx);
            break;
        case BORING_SYS_FD_READ:
            result = syscall_fd_read(frame->rdi, frame->rsi, frame->rdx);
            break;
        case BORING_SYS_FD_WRITE:
            result = syscall_fd_write(frame->rdi, frame->rsi, frame->rdx);
            break;
        case BORING_SYS_FD_CLOSE:
            result = syscall_fd_close(frame->rdi);
            break;
        case BORING_SYS_INPUT_CLAIM:
            result = syscall_input_claim();
            break;
        case BORING_SYS_INPUT_READ:
            result = syscall_input_read(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_INPUT_RELEASE:
            result = syscall_input_release();
            break;
        case BORING_SYS_MEMORY_ALLOC:
            result = syscall_memory_alloc(frame->rdi);
            break;
        case BORING_SYS_MEMORY_FREE:
            result = syscall_memory_free(frame->rdi);
            break;
        case BORING_SYS_BUFFER_CREATE:
            result = syscall_buffer_create(frame->rdi);
            break;
        case BORING_SYS_BUFFER_MAP:
            result = syscall_buffer_map(frame->rdi);
            break;
        case BORING_SYS_BUFFER_UNMAP:
            result = syscall_buffer_unmap(frame->rdi);
            break;
        case BORING_SYS_BUFFER_CLOSE:
            result = syscall_buffer_close(frame->rdi);
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
