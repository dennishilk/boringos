#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/elf_loader.h>
#include <boring/exception.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/runtime_smoke.h>
#include <boring/runtime_test.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/vmm.h>

#define RUNTIME_EXPECTED_MODULE_PATH "/boot/user/runtime-smoke.elf"
#define RUNTIME_EXPECTED_MODULE_STRING "boringos-runtime-smoke"
#define RUNTIME_GP_VECTOR 13ULL
#define RUNTIME_LOAD_EXPECTED 3U

struct runtime_test_state {
    struct process *process;
    struct boring_elf_image image;
    struct pmm_stats pmm_before;
    bool armed;
};

_Static_assert(sizeof(struct boring_runtime_smoke_result) == 112U,
               "runtime smoke result layout mismatch");

void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static struct runtime_test_state runtime_state;

static void runtime_fail(const char *check) __attribute__((noreturn));
static void runtime_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Native userspace runtime self-test FAILED: ");
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
        !limine_string_equals(module->path, RUNTIME_EXPECTED_MODULE_PATH) ||
        !limine_string_equals(module->string, RUNTIME_EXPECTED_MODULE_STRING)) {
        return false;
    }

    *bytes = (const uint8_t *)module->address;
    *size = (size_t)module->size;
    return true;
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

static bool segment_permissions_valid(const struct boring_elf_image *image) {
    static const uint64_t expected_vas[RUNTIME_LOAD_EXPECTED] = {
        BORING_RUNTIME_SMOKE_TEXT_VA,
        BORING_RUNTIME_SMOKE_RODATA_VA,
        BORING_RUNTIME_SMOKE_DATA_VA
    };
    static const uint32_t expected_flags[RUNTIME_LOAD_EXPECTED] = {
        BORING_ELF_PF_R | BORING_ELF_PF_X,
        BORING_ELF_PF_R,
        BORING_ELF_PF_R | BORING_ELF_PF_W
    };
    struct ring3_user_mapping_info stack_info;
    uint16_t segment_index;

    if ((image == NULL) || (image->process == NULL) ||
        (image->load_segment_count != RUNTIME_LOAD_EXPECTED)) {
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

        if ((segment->virtual_address != expected_vas[segment_index]) ||
            (segment->flags != expected_flags[segment_index]) ||
            (writable && executable)) {
            return false;
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

    return ring3_user_query_mapping(&image->process->address_space,
                                    image->stack_base, &stack_info) &&
           stack_info.writable && !stack_info.executable;
}

static void print_segment(uint16_t index,
                          const struct boring_elf_segment *segment) {
    const uint64_t end = segment->virtual_address + segment->memory_size;

    serial_write_string("Runtime PT_LOAD ");
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

bool runtime_test_exception_armed(void) {
    return runtime_state.armed;
}

void runtime_test_run(const struct boring_limine_module_response *modules) {
    const uint8_t *module = NULL;
    size_t module_size = 0U;
    struct boring_elf_validation validation;
    struct process *process = NULL;
    struct ring3_user_mapping_info entry_info;
    uint64_t bss_initial = UINT64_MAX;
    uint16_t index;

    runtime_state.process = NULL;
    runtime_state.armed = false;

    serial_write_string("Native userspace runtime test:\n");

    if (!module_find(modules, &module, &module_size)) {
        runtime_fail("boot-module-found");
    }
    serial_write_string("  boot-module-found: PASS\n");

    if (!boring_elf_validate(module, module_size,
                             (uintptr_t)BORING_RUNTIME_SMOKE_STACK_BASE,
                             (size_t)BORING_ELF_PAGE_SIZE, &validation) ||
        (validation.entry != (uintptr_t)BORING_RUNTIME_SMOKE_TEXT_VA) ||
        (validation.load_segment_count != RUNTIME_LOAD_EXPECTED)) {
        runtime_fail("elf64-runtime-image");
    }
    serial_write_string("  elf64-runtime-image: PASS\n");

    if (!x86_64_enable_nx() || !x86_64_nx_enabled()) {
        runtime_fail("nx-enabled");
    }
    serial_write_string("  nx-enabled: PASS\n");

    if (!process_init() || !syscall_init() ||
        !pmm_get_stats(&runtime_state.pmm_before) ||
        !process_create(&process) || (process == NULL) ||
        (process->pid != 1ULL)) {
        runtime_fail("process-created");
    }
    runtime_state.process = process;
    serial_write_string("  process-created: PASS\n");

    if (!boring_elf_load(process, module, module_size,
                         (uintptr_t)BORING_RUNTIME_SMOKE_STACK_BASE,
                         (size_t)BORING_ELF_PAGE_SIZE, &runtime_state.image)) {
        runtime_fail("load-runtime-image");
    }
    serial_write_string("  load-runtime-image: PASS\n");

    if (!segment_permissions_valid(&runtime_state.image)) {
        runtime_fail("segment-permissions");
    }
    serial_write_string("  segment-permissions: PASS\n");

    if (!ring3_user_query_mapping(&process->address_space,
                                  runtime_state.image.entry, &entry_info) ||
        !entry_info.executable || entry_info.writable) {
        runtime_fail("entry-executable");
    }
    serial_write_string("  entry-executable: PASS\n");

    if (!read_user_bytes(process, (uintptr_t)BORING_RUNTIME_SMOKE_BSS_VA,
                         &bss_initial, sizeof(bss_initial)) ||
        (bss_initial != 0ULL)) {
        runtime_fail("bss-loader-zeroed");
    }
    serial_write_string("  bss-loader-zeroed: PASS\n");
    serial_write_string("  user-stack-mapped: PASS\n");

    if (!ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        runtime_fail("higher-half-supervisor-only");
    }
    serial_write_string("  higher-half-supervisor-only: PASS\n");

    serial_write_string("Runtime module size: ");
    serial_write_u64((uint64_t)module_size);
    serial_write_string(" bytes\nRuntime ELF entry: ");
    serial_write_hex_u64((uint64_t)runtime_state.image.entry);
    serial_write_string("\nRuntime program headers: ");
    serial_write_u64((uint64_t)runtime_state.image.program_header_count);
    serial_write_string("\nRuntime PT_LOAD segments: ");
    serial_write_u64((uint64_t)runtime_state.image.load_segment_count);
    serial_write_string("\nRuntime process PID: ");
    serial_write_u64(process->pid);
    serial_write_string("\nRuntime process CR3: ");
    serial_write_hex_u64(process->address_space.root_physical);
    serial_write_string("\nRuntime user stack base: ");
    serial_write_hex_u64((uint64_t)runtime_state.image.stack_base);
    serial_write_string("\nRuntime user stack top: ");
    serial_write_hex_u64((uint64_t)runtime_state.image.stack_top);
    serial_write_string("\n");
    for (index = 0U; index < runtime_state.image.load_segment_count; ++index) {
        print_segment(index, &runtime_state.image.segments[index]);
    }

    x86_64_interrupts_disable();
    if (!process_activate(process) ||
        !ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        runtime_fail("process-activate");
    }

    runtime_state.armed = true;
    serial_write_string("Entering BoringOS C runtime at CPL3.\n");
    x86_64_enter_ring3(runtime_state.image.entry,
                       runtime_state.image.stack_top,
                       (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                       (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                       0U);
}

void runtime_test_handle_exception(const struct x86_64_trap_frame *frame) {
    struct boring_runtime_smoke_result result;
    struct syscall_stats syscall_stats;
    struct pmm_stats pmm_after;
    struct ring3_user_mapping_info fault_info;
    uint64_t bss_value = 0ULL;
    uint8_t fault_bytes[3] = { 0U, 0U, 0U };
    const uintptr_t handler_rsp = x86_64_read_rsp();
    struct process *bootstrap;
    const uint64_t expected_pid =
        (runtime_state.process != NULL) ? runtime_state.process->pid : UINT64_MAX;

    if (!runtime_state.armed || (frame == NULL) ||
        (runtime_state.process == NULL)) {
        runtime_fail("exception-frame");
    }
    runtime_state.armed = false;

    if (!read_user_bytes(runtime_state.process,
                         (uintptr_t)BORING_RUNTIME_SMOKE_DATA_VA,
                         &result, sizeof(result)) ||
        !read_user_bytes(runtime_state.process,
                         (uintptr_t)BORING_RUNTIME_SMOKE_BSS_VA,
                         &bss_value, sizeof(bss_value)) ||
        !read_user_bytes(runtime_state.process, (uintptr_t)frame->rip,
                         &fault_bytes[0], sizeof(fault_bytes))) {
        runtime_fail("result-read");
    }

    serial_write_string("Native C userspace:\n");

    if ((result.cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        ((result.cs & 0x3ULL) != 0x3ULL) || (result.entered != 1ULL)) {
        runtime_fail("c-entry");
    }
    serial_write_string("  c-entry: PASS\n");

    if (result.data_ok != 1ULL) {
        runtime_fail("initialized-data");
    }
    serial_write_string("  initialized-data: PASS\n");

    if ((result.bss_zero != 1ULL) ||
        (bss_value != BORING_RUNTIME_SMOKE_BSS_MARKER)) {
        runtime_fail("bss");
    }
    serial_write_string("  bss-initial-zero: PASS\n");
    serial_write_string("  bss-write: PASS\n");

    if (result.local_stack_ok != 1ULL) {
        runtime_fail("local-stack");
    }
    serial_write_string("  local-stack: PASS\n");

    if (result.strlen_ok != 1ULL) {
        runtime_fail("strlen");
    }
    serial_write_string("  strlen: PASS\n");

    if (result.memset_ok != 1ULL) {
        runtime_fail("memset");
    }
    serial_write_string("  memset: PASS\n");

    if (result.memcpy_ok != 1ULL) {
        runtime_fail("memcpy");
    }
    serial_write_string("  memcpy: PASS\n");

    if ((result.getpid_result != expected_pid) || (result.getpid_ok != 1ULL)) {
        runtime_fail("getpid");
    }
    serial_write_string("  getpid: PASS\n");

    if ((result.debug_result !=
         (uint64_t)BORING_RUNTIME_SMOKE_MESSAGE_LENGTH) ||
        (result.debug_ok != 1ULL)) {
        runtime_fail("debug-write");
    }
    serial_write_string("  debug-write: PASS\n");

    if ((result.sysret_resume != 1ULL) ||
        !syscall_get_stats(&syscall_stats) ||
        (syscall_stats.dispatch_count != 2ULL)) {
        runtime_fail("sysret-resume");
    }
    serial_write_string("  sysret-resume: PASS\n");

    if ((result.ready_to_return != 1ULL) ||
        (frame->r12 != (uint64_t)BORING_RUNTIME_SMOKE_MAIN_RETURN)) {
        runtime_fail("boring-main-return");
    }
    serial_write_string("  boring-main-return: PASS\n");

    if ((frame->vector != RUNTIME_GP_VECTOR) ||
        (frame->error_code != 0ULL) ||
        !exception_frame_originates_from_cpl3(frame) ||
        (frame->cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (frame->ss != (uint64_t)X86_64_GDT_USER_DATA_SELECTOR) ||
        !ring3_user_query_mapping(&runtime_state.process->address_space,
                                  (uintptr_t)frame->rip, &fault_info) ||
        !fault_info.executable || fault_info.writable ||
        (fault_bytes[0] != 0xfaU) || (fault_bytes[1] != 0x0fU) ||
        (fault_bytes[2] != 0x0bU)) {
        runtime_fail("final-cpl3-proof");
    }
    serial_write_string("  final-cpl3-proof: PASS\n");

    if (!descriptor_rsp0_stack_contains(handler_rsp) ||
        !descriptor_rsp0_stack_contains((uintptr_t)frame) ||
        descriptor_rsp0_stack_contains((uintptr_t)frame->rsp) ||
        (frame->rsp != (uint64_t)runtime_state.image.stack_top)) {
        runtime_fail("final-tss-rsp0");
    }
    serial_write_string("  final-tss-rsp0: PASS\n");

    serial_write_string("Runtime final fault RIP: ");
    serial_write_hex_u64(frame->rip);
    serial_write_string("\nRuntime boring_main return: ");
    serial_write_u64(frame->r12);
    serial_write_string("\nRuntime GETPID result: ");
    serial_write_u64(result.getpid_result);
    serial_write_string("\nRuntime DEBUG_WRITE result: ");
    serial_write_u64(result.debug_result);
    serial_write_string("\nRuntime syscall dispatches: ");
    serial_write_u64(syscall_stats.dispatch_count);
    serial_write_string("\n");

    bootstrap = process_bootstrap();
    if ((bootstrap == NULL) || !process_activate(bootstrap) ||
        !boring_elf_unload(&runtime_state.image) ||
        !process_mark_finished(runtime_state.process) ||
        !process_destroy(runtime_state.process) ||
        !pmm_get_stats(&pmm_after) ||
        (pmm_after.free_frames != runtime_state.pmm_before.free_frames)) {
        runtime_fail("cleanup");
    }
    serial_write_string("  cleanup: PASS\n");

    serial_write_string("BoringKernel native C runtime test passed.\n");
    x86_64_halt_forever();
}
