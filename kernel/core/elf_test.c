#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_loader.h>
#include <boring/elf_smoke.h>
#include <boring/elf_test.h>
#include <boring/exception.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/vmm.h>

#define ELF_EXPECTED_MODULE_PATH "/boot/user/elf-smoke.elf"
#define ELF_EXPECTED_MODULE_STRING "boringos-elf-smoke"
#define ELF_GP_VECTOR 13ULL
#define ELF_USER_RFLAGS 0x2ULL
#define ELF_MUT_E_TYPE 16U
#define ELF_MUT_E_MACHINE 18U
#define ELF_MUT_E_ENTRY 24U
#define ELF_MUT_E_PHOFF 32U
#define ELF_MUT_E_PHENTSIZE 54U
#define ELF_MUT_E_PHNUM 56U
#define ELF_PH_TYPE 0U
#define ELF_PH_FLAGS 4U
#define ELF_PH_OFFSET 8U
#define ELF_PH_VADDR 16U
#define ELF_PH_FILESZ 32U
#define ELF_PH_MEMSZ 40U
#define ELF_PT_LOAD 1U
#define ELF_ET_DYN 3U
#define ELF_EM_386 3U
#define ELF_CLASS32 1U
#define ELF_DATA_BIG_ENDIAN 2U
#define ELF_LOAD_EXPECTED 3U

struct elf_smoke_result {
    uint64_t cs;
    uint64_t entered;
    uint64_t bss_zero;
    uint64_t bss_write;
    uint64_t bss_value;
    uint64_t getpid;
    uint64_t getpid_pass;
    uint64_t debug_result;
    uint64_t debug_pass;
    uint64_t sysret_resume;
    uint64_t final_cli;
    uint64_t final_marker;
};

struct elf_test_state {
    struct process *process;
    struct boring_elf_image image;
    struct pmm_stats pmm_before;
    bool armed;
};

_Static_assert(sizeof(struct elf_smoke_result) == BORING_ELF_RESULT_SIZE,
               "ELF smoke result layout mismatch");

void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static struct elf_test_state elf_state;
static uint8_t elf_mutation_buffer[BORING_ELF_MODULE_MAX_SIZE]
    __attribute__((aligned(16)));

static void elf_fail(const char *check) __attribute__((noreturn));
static void elf_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("ELF userspace loader self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool canonical_higher(uintptr_t address) {
    return ((uint64_t)address >> 48U) == 0xffffULL;
}

static bool hhdm_pointer(const void *pointer) {
    struct vmm_stats stats;
    const uintptr_t address = (uintptr_t)pointer;

    return (pointer != NULL) && vmm_get_stats(&stats) &&
           canonical_higher(address) &&
           ((uint64_t)address >= stats.hhdm_offset);
}

static bool limine_string_equals(const char *actual, const char *expected) {
    size_t index = 0U;

    if (!hhdm_pointer(actual) || (expected == NULL)) {
        return false;
    }

    while (expected[index] != '\0') {
        if ((index >= 96U) || (actual[index] != expected[index])) {
            return false;
        }
        ++index;
    }
    return actual[index] == '\0';
}

static bool module_find(const struct boring_limine_module_response *response,
                        const uint8_t **bytes,
                        size_t *size) {
    const struct boring_limine_file *module;
    struct vmm_stats stats;
    uintptr_t address;
    uintptr_t last;

    if ((response == NULL) || (bytes == NULL) || (size == NULL) ||
        (response->module_count != 1ULL) ||
        !hhdm_pointer(response->modules) ||
        (response->modules[0] == NULL) ||
        !hhdm_pointer(response->modules[0]) ||
        !vmm_get_stats(&stats)) {
        return false;
    }

    module = response->modules[0];
    address = (uintptr_t)module->address;
    if (!hhdm_pointer(module->address) ||
        ((address & (uintptr_t)(BORING_ELF_PAGE_SIZE - 1ULL)) != 0U) ||
        (module->size == 0ULL) ||
        (module->size > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) ||
        (module->size > (uint64_t)SIZE_MAX) ||
        ((uint64_t)address > UINT64_MAX - (module->size - 1ULL))) {
        return false;
    }

    last = address + (uintptr_t)(module->size - 1ULL);
    if (!canonical_higher(last) || ((uint64_t)address < stats.hhdm_offset) ||
        !limine_string_equals(module->path, ELF_EXPECTED_MODULE_PATH) ||
        !limine_string_equals(module->string, ELF_EXPECTED_MODULE_STRING)) {
        return false;
    }

    *bytes = (const uint8_t *)module->address;
    *size = (size_t)module->size;
    return true;
}

static void copy_bytes(uint8_t *destination,
                       const uint8_t *source,
                       size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1U] << 8U));
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1U] << 8U) |
           ((uint32_t)bytes[offset + 2U] << 16U) |
           ((uint32_t)bytes[offset + 3U] << 24U);
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0ULL;
    unsigned int shift;

    for (shift = 0U; shift < 64U; shift += 8U) {
        value |= (uint64_t)bytes[offset + (size_t)(shift / 8U)] << shift;
    }
    return value;
}

static void write_u16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)(value & 0xffU);
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    unsigned int shift;

    for (shift = 0U; shift < 32U; shift += 8U) {
        bytes[offset + (size_t)(shift / 8U)] =
            (uint8_t)(value >> shift);
    }
}

static void write_u64(uint8_t *bytes, size_t offset, uint64_t value) {
    unsigned int shift;

    for (shift = 0U; shift < 64U; shift += 8U) {
        bytes[offset + (size_t)(shift / 8U)] =
            (uint8_t)(value >> shift);
    }
}

static bool find_load_phdr_offsets(const uint8_t *bytes,
                                   size_t size,
                                   size_t offsets[ELF_LOAD_EXPECTED]) {
    const uint64_t phoff = read_u64(bytes, ELF_MUT_E_PHOFF);
    const uint16_t phentsize = read_u16(bytes, ELF_MUT_E_PHENTSIZE);
    const uint16_t phnum = read_u16(bytes, ELF_MUT_E_PHNUM);
    uint16_t index;
    size_t count = 0U;

    if ((phentsize != 56U) || (phnum == 0U) ||
        (phnum > (uint16_t)BORING_ELF_MAX_PROGRAM_HEADERS)) {
        return false;
    }

    for (index = 0U; index < phnum; ++index) {
        uint64_t offset64 = phoff + ((uint64_t)index * (uint64_t)phentsize);
        size_t offset;

        if ((offset64 > (uint64_t)SIZE_MAX) ||
            (offset64 + 56ULL > (uint64_t)size)) {
            return false;
        }
        offset = (size_t)offset64;
        if (read_u32(bytes, offset + ELF_PH_TYPE) == ELF_PT_LOAD) {
            if (count >= (size_t)ELF_LOAD_EXPECTED) {
                return false;
            }
            offsets[count] = offset;
            ++count;
        }
    }

    return count == (size_t)ELF_LOAD_EXPECTED;
}

static bool mutation_rejected(const uint8_t *module,
                              size_t size,
                              unsigned int mutation) {
    struct boring_elf_validation validation;
    size_t loads[ELF_LOAD_EXPECTED];

    if ((module == NULL) || (size > sizeof(elf_mutation_buffer))) {
        return false;
    }
    copy_bytes(&elf_mutation_buffer[0], module, size);
    if (!find_load_phdr_offsets(&elf_mutation_buffer[0], size, loads)) {
        return false;
    }

    switch (mutation) {
        case 0U:
            elf_mutation_buffer[0] = 0U;
            break;
        case 1U:
            elf_mutation_buffer[4] = ELF_CLASS32;
            break;
        case 2U:
            elf_mutation_buffer[5] = ELF_DATA_BIG_ENDIAN;
            break;
        case 3U:
            write_u16(&elf_mutation_buffer[0], ELF_MUT_E_MACHINE, ELF_EM_386);
            break;
        case 4U:
            write_u16(&elf_mutation_buffer[0], ELF_MUT_E_TYPE, ELF_ET_DYN);
            break;
        case 5U:
            write_u64(&elf_mutation_buffer[0], ELF_MUT_E_PHOFF,
                      (uint64_t)size - 8ULL);
            break;
        case 6U: {
            const uint64_t memsz =
                read_u64(&elf_mutation_buffer[0], loads[0] + ELF_PH_MEMSZ);
            write_u64(&elf_mutation_buffer[0], loads[0] + ELF_PH_FILESZ,
                      memsz + 1ULL);
            break;
        }
        case 7U:
            write_u64(&elf_mutation_buffer[0], loads[0] + ELF_PH_OFFSET,
                      (uint64_t)BORING_ELF_MODULE_MAX_SIZE);
            break;
        case 8U:
            write_u64(&elf_mutation_buffer[0], loads[0] + ELF_PH_VADDR,
                      0xfffffffffffff000ULL);
            write_u64(&elf_mutation_buffer[0], loads[0] + ELF_PH_MEMSZ,
                      0x2000ULL);
            break;
        case 9U:
            write_u64(&elf_mutation_buffer[0], loads[0] + ELF_PH_VADDR,
                      0xffff800000000000ULL);
            break;
        case 10U:
            write_u64(&elf_mutation_buffer[0], loads[1] + ELF_PH_VADDR,
                      read_u64(&elf_mutation_buffer[0],
                               loads[0] + ELF_PH_VADDR));
            break;
        case 11U:
            write_u32(&elf_mutation_buffer[0], loads[0] + ELF_PH_FLAGS,
                      BORING_ELF_PF_R | BORING_ELF_PF_W | BORING_ELF_PF_X);
            break;
        case 12U:
            write_u64(&elf_mutation_buffer[0], ELF_MUT_E_ENTRY,
                      read_u64(&elf_mutation_buffer[0],
                               loads[2] + ELF_PH_VADDR));
            break;
        default:
            return false;
    }

    return !boring_elf_validate(&elf_mutation_buffer[0], size,
                                (uintptr_t)BORING_ELF_SMOKE_STACK_BASE,
                                (size_t)BORING_ELF_PAGE_SIZE, &validation);
}

static void require_mutation(const uint8_t *module,
                             size_t size,
                             unsigned int mutation,
                             const char *name) {
    if (!mutation_rejected(module, size, mutation)) {
        elf_fail(name);
    }
    serial_write_string("  ");
    serial_write_string(name);
    serial_write_string(": PASS\n");
}

static bool read_user_bytes(const struct process *process,
                            uintptr_t user_address,
                            void *destination,
                            size_t length) {
    struct vmm_stats stats;
    uint8_t *output = (uint8_t *)destination;
    uintptr_t current = user_address;
    size_t remaining = length;

    if ((process == NULL) || (destination == NULL) || (length == 0U) ||
        !process_is_alive(process) || process->address_space.bootstrap ||
        !ring3_user_range_valid(user_address, length) ||
        !vmm_get_stats(&stats)) {
        return false;
    }

    while (remaining != 0U) {
        uint64_t physical;
        uint64_t kernel_virtual;
        const size_t page_offset =
            (size_t)((uint64_t)current & (BORING_ELF_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)BORING_ELF_PAGE_SIZE - page_offset;
        const uint8_t *source;
        size_t index;

        if (chunk > remaining) {
            chunk = remaining;
        }
        if (!ring3_user_translate(&process->address_space, current, false,
                                  &physical) ||
            (physical > UINT64_MAX - stats.hhdm_offset)) {
            return false;
        }
        kernel_virtual = stats.hhdm_offset + physical;
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

static bool permissions_valid(const struct boring_elf_image *image) {
    bool saw_rx = false;
    bool saw_r = false;
    bool saw_rw = false;
    uint16_t segment_index;
    struct ring3_user_mapping_info stack_info;

    if ((image == NULL) || (image->process == NULL) ||
        (image->load_segment_count != ELF_LOAD_EXPECTED)) {
        return false;
    }

    for (segment_index = 0U; segment_index < image->load_segment_count;
         ++segment_index) {
        const struct boring_elf_segment *segment =
            &image->segments[segment_index];
        const bool writable =
            (segment->flags & BORING_ELF_PF_W) != 0U;
        const bool executable =
            (segment->flags & BORING_ELF_PF_X) != 0U;
        uint64_t page_index;

        if (writable && executable) {
            return false;
        }
        if (executable && !writable) {
            saw_rx = true;
        } else if (writable && !executable) {
            saw_rw = true;
        } else {
            saw_r = true;
        }

        for (page_index = 0ULL; page_index < segment->page_count;
             ++page_index) {
            const uintptr_t virtual_address = (uintptr_t)(
                segment->virtual_address + page_index * BORING_ELF_PAGE_SIZE);
            struct ring3_user_mapping_info info;

            if (!ring3_user_query_mapping(&image->process->address_space,
                                          virtual_address, &info) ||
                (info.writable != writable) ||
                (info.executable != executable)) {
                return false;
            }
        }
    }

    return saw_rx && saw_r && saw_rw &&
           ring3_user_query_mapping(&image->process->address_space,
                                    image->stack_base, &stack_info) &&
           stack_info.writable && !stack_info.executable;
}

static void bounded_stack_mapping(struct process *process,
                                   const uint8_t *module, size_t module_size) {
    const uintptr_t base = (uintptr_t)BORING_ELF_SMOKE_STACK_BASE;
    struct boring_elf_image image;
    struct boring_elf_validation validation;
    struct ring3_user_mapping_info mapping;
    size_t page;
    if (boring_elf_load(process, module, module_size, base,
                         3U * (size_t)BORING_ELF_PAGE_SIZE, &image) ||
        boring_elf_load(process, module, module_size, base,
                         (size_t)BORING_ELF_PAGE_SIZE + 1U, &image) ||
        !boring_elf_validate(module, module_size, base,
                             2U * (size_t)BORING_ELF_PAGE_SIZE, &validation) ||
        !boring_elf_load(process, module, module_size, base,
                         2U * (size_t)BORING_ELF_PAGE_SIZE, &image) ||
        (image.owned_page_count != validation.total_image_pages + 2U) ||
        (image.stack_top != base + 2U * BORING_ELF_PAGE_SIZE) ||
        ring3_user_query_mapping(&process->address_space,
                                 base - BORING_ELF_PAGE_SIZE, &mapping)) {
        elf_fail("bounded-stack-mapping");
    }
    for (page = 0U; page < 2U; ++page) {
        const uintptr_t address = base + page * BORING_ELF_PAGE_SIZE;
        uint8_t boundary[2] = {1U, 1U};
        if (!ring3_user_query_mapping(&process->address_space, address, &mapping) ||
            !mapping.writable || mapping.executable ||
            !read_user_bytes(process, address, &boundary[0], 1U) ||
            !read_user_bytes(process, address + BORING_ELF_PAGE_SIZE - 1U,
                              &boundary[1], 1U) || boundary[0] || boundary[1]) {
            elf_fail("stack-pages-zero-rw-nx");
        }
    }
    if (!boring_elf_unload(&image) || image.owned_page_count != 0U ||
        ring3_user_query_mapping(&process->address_space, base, &mapping) ||
        ring3_user_query_mapping(&process->address_space,
                                 base + BORING_ELF_PAGE_SIZE, &mapping)) {
        elf_fail("stack-pages-unload");
    }
    /* A PT_LOAD ending at stack_base would occupy the lower guard page. */
    if (boring_elf_validate(module, module_size,
                            (uintptr_t)BORING_ELF_SMOKE_DATA_VA + BORING_ELF_PAGE_SIZE,
                            (size_t)BORING_ELF_PAGE_SIZE, &validation)) {
        elf_fail("stack-guard-overlap-rejected");
    }
    serial_write_string("  bounded-stack-mapping: PASS\n");
}

static void print_segment(uint16_t index,
                          const struct boring_elf_segment *segment) {
    uint64_t end = segment->virtual_address + segment->memory_size;

    serial_write_string("PT_LOAD ");
    serial_write_u64((uint64_t)index);
    serial_write_string(": ");
    serial_write_hex_u64(segment->virtual_address);
    serial_write_string(" - ");
    serial_write_hex_u64(end);
    serial_write_string(" filesz=");
    serial_write_u64(segment->file_size);
    serial_write_string(" memsz=");
    serial_write_u64(segment->memory_size);
    serial_write_string(" flags=");
    serial_write_string((segment->flags & BORING_ELF_PF_R) != 0U ? "R" : "-");
    serial_write_string((segment->flags & BORING_ELF_PF_W) != 0U ? "W" : "-");
    serial_write_string((segment->flags & BORING_ELF_PF_X) != 0U ? "X" : "-");
    serial_write_string("\n");
}

bool elf_test_exception_armed(void) {
    return elf_state.armed;
}

void elf_test_run(const struct boring_limine_module_response *modules) {
    const uint8_t *module = NULL;
    size_t module_size = 0U;
    struct boring_elf_validation validation;
    struct pmm_stats malformed_before;
    struct pmm_stats malformed_after;
    struct process *process = NULL;
    uint64_t bss_initial = UINT64_MAX;
    uint16_t index;

    elf_state.process = NULL;
    elf_state.armed = false;

    serial_write_string("ELF userspace loader test:\n");

    if (!module_find(modules, &module, &module_size)) {
        elf_fail("boot-module-found");
    }
    serial_write_string("  boot-module-found: PASS\n");

    if (!boring_elf_validate(module, module_size,
                             (uintptr_t)BORING_ELF_SMOKE_STACK_BASE,
                             (size_t)BORING_ELF_PAGE_SIZE, &validation) ||
        (validation.entry != (uintptr_t)BORING_ELF_SMOKE_TEXT_VA) ||
        (validation.load_segment_count != ELF_LOAD_EXPECTED)) {
        elf_fail("elf64-header");
    }
    serial_write_string("  elf64-header: PASS\n");
    serial_write_string("  program-table-bounds: PASS\n");

    if (!pmm_get_stats(&malformed_before)) {
        elf_fail("malformed-no-allocation");
    }
    require_mutation(module, module_size, 0U, "malformed-magic-rejected");
    require_mutation(module, module_size, 1U, "elf32-rejected");
    require_mutation(module, module_size, 2U, "wrong-endian-rejected");
    require_mutation(module, module_size, 3U, "wrong-machine-rejected");
    require_mutation(module, module_size, 4U, "unsupported-type-rejected");
    require_mutation(module, module_size, 5U, "truncated-phdr-rejected");
    require_mutation(module, module_size, 6U, "filesz-memsz-validation");
    require_mutation(module, module_size, 7U, "file-range-rejected");
    require_mutation(module, module_size, 8U, "virtual-overflow-rejected");
    require_mutation(module, module_size, 9U, "higher-half-segment-rejected");
    require_mutation(module, module_size, 10U, "overlapping-segment-rejected");
    require_mutation(module, module_size, 11U, "wx-segment-rejected");
    require_mutation(module, module_size, 12U, "entry-outside-exec-rejected");
    if (!pmm_get_stats(&malformed_after) ||
        (malformed_after.free_frames != malformed_before.free_frames)) {
        elf_fail("malformed-no-allocation");
    }
    serial_write_string("  malformed-no-allocation: PASS\n");

    if (!x86_64_enable_nx() || !x86_64_nx_enabled()) {
        elf_fail("nx-enabled");
    }
    serial_write_string("  nx-enabled: PASS\n");

    if (!process_init() || !syscall_init() ||
        !pmm_get_stats(&elf_state.pmm_before) ||
        !process_create(&process) || (process == NULL) ||
        (process->pid != 1ULL)) {
        elf_fail("process-created");
    }
    elf_state.process = process;
    serial_write_string("  process-created: PASS\n");

    bounded_stack_mapping(process, module, module_size);

    if (!boring_elf_load(process, module, module_size,
                         (uintptr_t)BORING_ELF_SMOKE_STACK_BASE,
                         (size_t)BORING_ELF_PAGE_SIZE, &elf_state.image)) {
        elf_fail("load-segments");
    }
    serial_write_string("  load-segments: PASS\n");

    if (!permissions_valid(&elf_state.image)) {
        elf_fail("segment-permissions");
    }
    serial_write_string("  segment-permissions: PASS\n");

    {
        struct ring3_user_mapping_info entry_info;
        if (!ring3_user_query_mapping(&process->address_space,
                                      elf_state.image.entry, &entry_info) ||
            !entry_info.executable || entry_info.writable) {
            elf_fail("entry-executable");
        }
    }
    serial_write_string("  entry-executable: PASS\n");

    if (!read_user_bytes(process, (uintptr_t)BORING_ELF_SMOKE_BSS_VA,
                         &bss_initial, sizeof(bss_initial)) ||
        (bss_initial != 0ULL)) {
        elf_fail("bss-zeroed");
    }
    serial_write_string("  bss-zeroed: PASS\n");
    serial_write_string("  user-stack-mapped: PASS\n");

    if (!ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        elf_fail("higher-half-supervisor-only");
    }
    serial_write_string("  higher-half-supervisor-only: PASS\n");

    serial_write_string("ELF module size: ");
    serial_write_u64((uint64_t)module_size);
    serial_write_string(" bytes\nELF entry: ");
    serial_write_hex_u64((uint64_t)elf_state.image.entry);
    serial_write_string("\nProgram headers: ");
    serial_write_u64((uint64_t)elf_state.image.program_header_count);
    serial_write_string("\nPT_LOAD segments: ");
    serial_write_u64((uint64_t)elf_state.image.load_segment_count);
    serial_write_string("\nProcess PID: ");
    serial_write_u64(process->pid);
    serial_write_string("\nProcess CR3: ");
    serial_write_hex_u64(process->address_space.root_physical);
    serial_write_string("\nUser stack base: ");
    serial_write_hex_u64((uint64_t)elf_state.image.stack_base);
    serial_write_string("\nUser stack top: ");
    serial_write_hex_u64((uint64_t)elf_state.image.stack_top);
    serial_write_string("\n");
    for (index = 0U; index < elf_state.image.load_segment_count; ++index) {
        print_segment(index, &elf_state.image.segments[index]);
    }

    x86_64_interrupts_disable();
    if (!process_activate(process) ||
        !ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        elf_fail("process-activate");
    }

    elf_state.armed = true;
    serial_write_string("Entering ELF userspace at CPL3.\n");
    x86_64_enter_ring3(elf_state.image.entry,
                       elf_state.image.stack_top,
                       (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                       (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                       0U);
}

void elf_test_handle_exception(const struct x86_64_trap_frame *frame) {
    struct elf_smoke_result result;
    struct syscall_stats syscall_stats;
    struct pmm_stats pmm_after;
    uint64_t bss_value = 0ULL;
    uint8_t fault_bytes[3] = { 0U, 0U, 0U };
    const uintptr_t handler_rsp = x86_64_read_rsp();
    struct process *bootstrap;
    const uint64_t expected_pid =
        (elf_state.process != NULL) ? elf_state.process->pid : UINT64_MAX;

    if (!elf_state.armed || (frame == NULL) ||
        (elf_state.process == NULL)) {
        elf_fail("exception-frame");
    }
    elf_state.armed = false;

    if (!read_user_bytes(elf_state.process,
                         (uintptr_t)BORING_ELF_SMOKE_RESULT_VA,
                         &result, sizeof(result)) ||
        !read_user_bytes(elf_state.process,
                         (uintptr_t)BORING_ELF_SMOKE_BSS_VA,
                         &bss_value, sizeof(bss_value)) ||
        !read_user_bytes(elf_state.process, (uintptr_t)frame->rip,
                         &fault_bytes[0], sizeof(fault_bytes))) {
        elf_fail("result-read");
    }

    serial_write_string("ELF userspace:\n");
    if ((result.cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        ((result.cs & 0x3ULL) != 0x3ULL) || (result.entered != 1ULL)) {
        elf_fail("entered-cpl3");
    }
    serial_write_string("  entered-cpl3: PASS\n");

    if ((result.bss_zero != 1ULL) ||
        (result.bss_write != 1ULL) ||
        (result.bss_value != BORING_ELF_SMOKE_BSS_MARKER) ||
        (bss_value != BORING_ELF_SMOKE_BSS_MARKER)) {
        elf_fail("bss-write");
    }
    serial_write_string("  bss-initial-zero: PASS\n");
    serial_write_string("  bss-write: PASS\n");

    if ((result.getpid != expected_pid) || (result.getpid_pass != 1ULL)) {
        elf_fail("getpid");
    }
    serial_write_string("  getpid: PASS\n");

    if ((result.debug_result != (uint64_t)BORING_ELF_SMOKE_MESSAGE_LENGTH) ||
        (result.debug_pass != 1ULL)) {
        elf_fail("debug-write");
    }
    serial_write_string("  debug-write: PASS\n");

    if ((result.sysret_resume != 1ULL) ||
        (result.final_cli != 1ULL) ||
        (result.final_marker != BORING_ELF_SMOKE_FINAL_MARKER) ||
        !syscall_get_stats(&syscall_stats) ||
        (syscall_stats.dispatch_count != 2ULL)) {
        elf_fail("sysret-resume");
    }
    serial_write_string("  sysret-resume: PASS\n");

    if ((frame->vector != ELF_GP_VECTOR) || (frame->error_code != 0ULL) ||
        !exception_frame_originates_from_cpl3(frame) ||
        (frame->cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (frame->ss != (uint64_t)X86_64_GDT_USER_DATA_SELECTOR) ||
        (fault_bytes[0] != 0xfaU) || (fault_bytes[1] != 0x0fU) ||
        (fault_bytes[2] != 0x0bU)) {
        elf_fail("final-cpl3-proof");
    }
    serial_write_string("  final-cpl3-proof: PASS\n");

    if (!descriptor_rsp0_stack_contains(handler_rsp) ||
        !descriptor_rsp0_stack_contains((uintptr_t)frame) ||
        descriptor_rsp0_stack_contains((uintptr_t)frame->rsp) ||
        (frame->rsp != (uint64_t)elf_state.image.stack_top)) {
        elf_fail("final-tss-rsp0");
    }
    serial_write_string("  final-tss-rsp0: PASS\n");

    serial_write_string("ELF final fault RIP: ");
    serial_write_hex_u64(frame->rip);
    serial_write_string("\nELF GETPID result: ");
    serial_write_u64(result.getpid);
    serial_write_string("\nELF DEBUG_WRITE result: ");
    serial_write_u64(result.debug_result);
    serial_write_string("\nELF syscall dispatches: ");
    serial_write_u64(syscall_stats.dispatch_count);
    serial_write_string("\n");

    bootstrap = process_bootstrap();
    if ((bootstrap == NULL) || !process_activate(bootstrap) ||
        !boring_elf_unload(&elf_state.image) ||
        !process_mark_finished(elf_state.process) ||
        !process_destroy(elf_state.process) ||
        !pmm_get_stats(&pmm_after) ||
        (pmm_after.free_frames != elf_state.pmm_before.free_frames)) {
        elf_fail("cleanup");
    }
    serial_write_string("  cleanup: PASS\n");

    serial_write_string("BoringKernel ELF userspace loader test passed.\n");
    x86_64_halt_forever();
}
