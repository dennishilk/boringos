#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device_test.h>
#include <boring/boringfs_ro_test.h>
#include <boring/boot_protocol.h>
#include <boring/boot_dashboard.h>
#include <boring/framebuffer.h>
#include <boring/context.h>
#include <boring/cpu.h>
#include <boring/cpu_inventory.h>
#include <boring/exception.h>
#include <boring/elf_boot.h>
#include <boring/heap.h>
#include <boring/init_test.h>
#include <boring/ipc_test.h>
#include <boring/irq.h>
#include <boring/kernel.h>
#include <boring/pmm.h>
#include <boring/pci_inventory.h>
#include <boring/preemption_test.h>
#include <boring/process.h>
#include <boring/process_test.h>
#include <boring/ramfs_test.h>
#include <boring/ring3_test.h>
#include <boring/serial.h>
#include <boring/shell_test.h>
#include <boring/smbios.h>
#include <boring/syscall_test.h>
#include <boring/task.h>
#include <boring/timer.h>
#include <boring/vfs_test.h>
#include <boring/virtio_blk_test.h>
#include <boring/vmm.h>

#define VMM_TEST_PATTERN 0x424f52494e474f53ULL
#define BORING_TEST_MODE_NORMAL 0
#define BORING_TEST_MODE_DIVIDE 1
#define BORING_TEST_MODE_PAGEFAULT 2
#define BORING_TEST_MODE_RING3 3
#define BORING_TEST_MODE_SYSCALL 4
#define BORING_TEST_MODE_RUNTIME 5
#define BORING_TEST_MODE_CONSOLE 6
#define BORING_TEST_MODE_VFS 7
#define BORING_TEST_MODE_RAMFS 8
#define BORING_TEST_MODE_INIT 9
#define BORING_TEST_MODE_SHELL 10
#define BORING_TEST_MODE_BLOCK 11
#define BORING_TEST_MODE_VIRTIO_BLOCK 12
#define BORING_TEST_MODE_BORINGFS_RO 13
#define BORING_TEST_MODE_BORINGFS_RW 14
#define BORING_TEST_MODE_PERSISTENT_ROOT 15
#define BORING_TEST_MODE_M33_IPC 16
#define IRQ_TEST_TICKS 10ULL
#define IRQ_TEST_SPIN_LIMIT 500000000ULL
#define BOOTSTRAP_TIMER_FREQUENCY_HZ 100U
#define TASK_TEST_ITERATIONS 3ULL
#define TASK_TEST_SEQUENCE_LENGTH 6U
#define TASK_TEST_TIMER_SPIN_LIMIT 50000000ULL

#ifndef BORING_TEST_MODE
#define BORING_TEST_MODE BORING_TEST_MODE_NORMAL
#endif

#if (BORING_TEST_MODE != BORING_TEST_MODE_NORMAL) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_DIVIDE) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_PAGEFAULT) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_RING3) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_SYSCALL) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_RUNTIME) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_CONSOLE) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_VFS) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_RAMFS) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_INIT) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_SHELL) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_BLOCK) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_VIRTIO_BLOCK) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_BORINGFS_RO) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_BORINGFS_RW) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_PERSISTENT_ROOT) && \
    (BORING_TEST_MODE != BORING_TEST_MODE_M33_IPC)
#error "unsupported BoringKernel test mode"
#endif

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = BORING_LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = BORING_LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_memmap_request limine_memmap_request = {
    .id = BORING_LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0ULL,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_hhdm_request limine_hhdm_request = {
    .id = BORING_LIMINE_HHDM_REQUEST_ID,
    .revision = 0ULL,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_paging_mode_request limine_paging_mode_request = {
    .id = BORING_LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 1ULL,
    .response = 0,
    .mode = BORING_LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = BORING_LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = BORING_LIMINE_PAGING_MODE_X86_64_4LVL
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = BORING_LIMINE_REQUESTS_END_MARKER;

static bool pmm_self_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("PMM self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    return false;
}

static bool pmm_self_test(void) {
    enum { TEST_FRAME_COUNT = 4 };
    uint64_t frames[TEST_FRAME_COUNT];
    uint64_t replacement;
    uint64_t first_index;
    uint64_t second_index;
    struct pmm_stats before;
    struct pmm_stats after;

    serial_write_string("PMM self-test:\n");

    if (!pmm_get_stats(&before) ||
        (before.free_frames < (uint64_t)TEST_FRAME_COUNT)) {
        return pmm_self_test_fail("allocate");
    }

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        if (!pmm_alloc_frame(&frames[first_index])) {
            return pmm_self_test_fail("allocate");
        }
    }
    serial_write_string("  allocate: PASS\n");

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        for (second_index = first_index + 1ULL;
             second_index < (uint64_t)TEST_FRAME_COUNT; ++second_index) {
            if (frames[first_index] == frames[second_index]) {
                return pmm_self_test_fail("unique");
            }
        }
    }
    serial_write_string("  unique: PASS\n");

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        if ((frames[first_index] % PMM_PAGE_SIZE) != 0ULL) {
            return pmm_self_test_fail("aligned");
        }
    }
    serial_write_string("  aligned: PASS\n");

    for (first_index = 0ULL;
         first_index < (uint64_t)TEST_FRAME_COUNT; ++first_index) {
        if (!pmm_frame_is_usable(frames[first_index])) {
            return pmm_self_test_fail("usable");
        }
    }
    serial_write_string("  usable: PASS\n");

    if (!pmm_get_stats(&after) ||
        (after.free_frames !=
         (before.free_frames - (uint64_t)TEST_FRAME_COUNT))) {
        return pmm_self_test_fail("bookkeeping");
    }

    if (!pmm_free_frame(frames[1]) ||
        !pmm_alloc_frame(&replacement) ||
        (replacement != frames[1])) {
        return pmm_self_test_fail("free");
    }
    serial_write_string("  free: PASS\n");

    if (!pmm_free_frame(replacement) ||
        pmm_free_frame(replacement) ||
        !pmm_free_frame(frames[0]) ||
        !pmm_free_frame(frames[2]) ||
        !pmm_free_frame(frames[3])) {
        return pmm_self_test_fail("invalid-free");
    }
    serial_write_string("  invalid-free: PASS\n");

    if (!pmm_get_stats(&after) ||
        (after.free_frames != before.free_frames) ||
        (after.usable_frames != before.usable_frames)) {
        return pmm_self_test_fail("bookkeeping");
    }
    serial_write_string("  bookkeeping: PASS\n");

    return true;
}

static bool vmm_self_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("VMM self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    return false;
}

static bool vmm_self_test(void) {
    struct pmm_stats pmm_before;
    struct pmm_stats pmm_after;
    struct vmm_stats vmm_before;
    struct vmm_stats vmm_after_map;
    struct vmm_stats vmm_after_unmap;
    const uintptr_t test_virtual = vmm_test_virtual_address();
    volatile uint64_t *const test_pointer =
        (volatile uint64_t *)test_virtual;
    uint64_t test_physical;
    uint64_t translated = 0ULL;
    uint64_t translation_result;
    uint64_t read_back;
    uint64_t page_table_frames;

    serial_write_string("VMM self-test:\n");

    if (!pmm_get_stats(&pmm_before) || !vmm_get_stats(&vmm_before) ||
        !pmm_alloc_frame(&test_physical)) {
        return vmm_self_test_fail("frame-allocation");
    }
    serial_write_string("  frame-allocation: PASS\n");

    if (vmm_translate(test_virtual, &translated)) {
        return vmm_self_test_fail("unmapped-check");
    }
    serial_write_string("  unmapped-check: PASS\n");

    if (!vmm_map_page(test_virtual, test_physical, VMM_FLAG_WRITABLE) ||
        !vmm_get_stats(&vmm_after_map) ||
        (vmm_after_map.owned_page_table_frames <
         vmm_before.owned_page_table_frames)) {
        return vmm_self_test_fail("map");
    }
    page_table_frames = vmm_after_map.owned_page_table_frames -
                        vmm_before.owned_page_table_frames;
    serial_write_string("  map: PASS\n");

    if (!vmm_translate(test_virtual, &translated) ||
        (translated != test_physical)) {
        return vmm_self_test_fail("translate");
    }
    translation_result = translated;
    serial_write_string("  translate: PASS\n");

    *test_pointer = VMM_TEST_PATTERN;
    read_back = *test_pointer;
    if (read_back != VMM_TEST_PATTERN) {
        return vmm_self_test_fail("write-read");
    }
    *test_pointer = 0ULL;
    serial_write_string("  write-read: PASS\n");

    if (!vmm_unmap_page(test_virtual) ||
        vmm_translate(test_virtual, &translated)) {
        return vmm_self_test_fail("unmap");
    }
    serial_write_string("  unmap: PASS\n");

    if (!vmm_get_stats(&vmm_after_unmap) ||
        (vmm_after_unmap.owned_page_table_frames !=
         vmm_before.owned_page_table_frames) ||
        !pmm_free_frame(test_physical) ||
        !pmm_get_stats(&pmm_after) ||
        (pmm_after.free_frames != pmm_before.free_frames) ||
        (pmm_after.usable_frames != pmm_before.usable_frames)) {
        return vmm_self_test_fail("frame-release");
    }
    serial_write_string("  frame-release: PASS\n");

    serial_write_string("Page-table frames allocated: ");
    serial_write_u64(page_table_frames);
    serial_write_string("\n");
    serial_write_string("Test physical frame: ");
    serial_write_u64(test_physical);
    serial_write_string("\n");
    serial_write_string("Translation result: ");
    serial_write_u64(translation_result);
    serial_write_string("\n");
    serial_write_string("Test pattern: 0x424F52494E474F53\n");

    return true;
}

static bool heap_self_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Heap self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    return false;
}

static bool heap_ranges_overlap(uintptr_t first,
                                size_t first_size,
                                uintptr_t second,
                                size_t second_size) {
    uintptr_t first_end;
    uintptr_t second_end;

    if ((first_size > (size_t)(UINTPTR_MAX - first)) ||
        (second_size > (size_t)(UINTPTR_MAX - second))) {
        return true;
    }

    first_end = first + (uintptr_t)first_size;
    second_end = second + (uintptr_t)second_size;
    return (first < second_end) && (second < first_end);
}

static uint8_t heap_test_pattern(size_t block_index, size_t offset) {
    switch (block_index) {
        case 0U:
            return 0xaaU;
        case 1U:
            return 0x55U;
        case 2U:
            return (uint8_t)(offset & 0xffU);
        case 3U:
            return 0xa5U;
        case 4U:
            return (uint8_t)((offset + 0x33U) & 0xffU);
        default:
            return 0x3cU;
    }
}

static bool heap_write_read_test(void *const *blocks,
                                 const size_t *sizes,
                                 size_t count) {
    size_t block_index;

    for (block_index = 0U; block_index < count; ++block_index) {
        size_t offset;
        volatile uint8_t *bytes =
            (volatile uint8_t *)blocks[block_index];

        for (offset = 0U; offset < sizes[block_index]; ++offset) {
            bytes[offset] = heap_test_pattern(block_index, offset);
        }
    }

    for (block_index = 0U; block_index < count; ++block_index) {
        size_t offset;
        volatile const uint8_t *bytes =
            (volatile const uint8_t *)blocks[block_index];

        for (offset = 0U; offset < sizes[block_index]; ++offset) {
            if (bytes[offset] != heap_test_pattern(block_index, offset)) {
                return false;
            }
        }
    }

    return true;
}

static bool heap_self_test(void) {
    enum { HEAP_TEST_ALLOCATION_COUNT = 6 };
    static const size_t sizes[HEAP_TEST_ALLOCATION_COUNT] = {
        1U, 16U, 64U, 200U, 6000U, 4096U
    };
    void *blocks[HEAP_TEST_ALLOCATION_COUNT] = { NULL };
    void *reused;
    struct heap_stats heap_before;
    struct heap_stats heap_after_alloc;
    struct heap_stats heap_final;
    struct pmm_stats pmm_before_growth;
    struct pmm_stats pmm_after_growth;
    struct pmm_stats pmm_final;
    uint64_t growth_mappings;
    uint64_t growth_pmm_frames;
    size_t first_index;
    size_t second_index;

    serial_write_string("Heap self-test:\n");

    if (!heap_get_stats(&heap_before) ||
        !pmm_get_stats(&pmm_before_growth)) {
        return heap_self_test_fail("allocate");
    }

    for (first_index = 0U;
         first_index < (size_t)HEAP_TEST_ALLOCATION_COUNT; ++first_index) {
        blocks[first_index] = kmalloc(sizes[first_index]);
        if (blocks[first_index] == NULL) {
            return heap_self_test_fail("allocate");
        }
    }
    if (kmalloc(0U) != NULL) {
        return heap_self_test_fail("allocate");
    }
    serial_write_string("  allocate: PASS\n");

    for (first_index = 0U;
         first_index < (size_t)HEAP_TEST_ALLOCATION_COUNT; ++first_index) {
        if (((uintptr_t)blocks[first_index] &
             ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL) {
            return heap_self_test_fail("alignment");
        }
    }
    serial_write_string("  alignment: PASS\n");

    for (first_index = 0U;
         first_index < (size_t)HEAP_TEST_ALLOCATION_COUNT; ++first_index) {
        for (second_index = first_index + 1U;
             second_index < (size_t)HEAP_TEST_ALLOCATION_COUNT;
             ++second_index) {
            if (heap_ranges_overlap((uintptr_t)blocks[first_index],
                                    sizes[first_index],
                                    (uintptr_t)blocks[second_index],
                                    sizes[second_index])) {
                return heap_self_test_fail("non-overlap");
            }
        }
    }
    serial_write_string("  non-overlap: PASS\n");

    if (!heap_write_read_test(blocks, sizes,
                              (size_t)HEAP_TEST_ALLOCATION_COUNT)) {
        return heap_self_test_fail("write-read");
    }
    serial_write_string("  write-read: PASS\n");

    if (!heap_get_stats(&heap_after_alloc) ||
        !pmm_get_stats(&pmm_after_growth) ||
        (heap_after_alloc.mapped_pages <= heap_before.mapped_pages) ||
        (heap_after_alloc.allocation_count !=
         (uint64_t)HEAP_TEST_ALLOCATION_COUNT) ||
        (pmm_before_growth.free_frames <= pmm_after_growth.free_frames)) {
        return heap_self_test_fail("growth");
    }

    growth_mappings = heap_after_alloc.mapped_pages -
                      heap_before.mapped_pages;
    growth_pmm_frames = pmm_before_growth.free_frames -
                        pmm_after_growth.free_frames;

    for (first_index = (size_t)heap_before.mapped_pages;
         first_index < (size_t)heap_after_alloc.mapped_pages;
         ++first_index) {
        uintptr_t virtual_address;
        uint64_t physical_address;

        if (first_index >
            (size_t)((UINTPTR_MAX - heap_before.virtual_base) /
                     (uintptr_t)VMM_PAGE_SIZE)) {
            return heap_self_test_fail("growth");
        }
        virtual_address = heap_before.virtual_base +
            ((uintptr_t)first_index * (uintptr_t)VMM_PAGE_SIZE);
        if (!vmm_translate(virtual_address, &physical_address) ||
            !pmm_frame_is_usable(physical_address)) {
            return heap_self_test_fail("growth");
        }
    }
    serial_write_string("  growth: PASS\n");

    if (!kfree(blocks[2])) {
        return heap_self_test_fail("free");
    }
    serial_write_string("  free: PASS\n");

    reused = kmalloc(sizes[2]);
    if ((reused == NULL) || (reused != blocks[2])) {
        return heap_self_test_fail("reuse");
    }
    serial_write_string("  reuse: PASS\n");

    if (!kfree(reused) || kfree(reused)) {
        return heap_self_test_fail("double-free");
    }
    blocks[2] = NULL;
    serial_write_string("  double-free: PASS\n");

    if (kfree((void *)(heap_before.virtual_base -
                       (uintptr_t)KERNEL_HEAP_ALIGNMENT)) ||
        kfree((void *)((uintptr_t)blocks[3] +
                       (uintptr_t)KERNEL_HEAP_ALIGNMENT))) {
        return heap_self_test_fail("invalid-free");
    }
    serial_write_string("  invalid-free: PASS\n");

    for (first_index = 0U;
         first_index < (size_t)HEAP_TEST_ALLOCATION_COUNT; ++first_index) {
        if ((blocks[first_index] != NULL) &&
            !kfree(blocks[first_index])) {
            return heap_self_test_fail("bookkeeping");
        }
    }

    if (!heap_get_stats(&heap_final) || !pmm_get_stats(&pmm_final) ||
        (heap_final.allocation_count != 0ULL) ||
        (heap_final.used_bytes != 0ULL) ||
        (heap_final.free_bytes == 0ULL) ||
        (heap_final.mapped_pages != heap_after_alloc.mapped_pages) ||
        (pmm_final.free_frames != pmm_after_growth.free_frames)) {
        return heap_self_test_fail("bookkeeping");
    }
    serial_write_string("  bookkeeping: PASS\n");

    serial_write_string("Allocation sizes tested: 1,16,64,200,6000,4096 bytes\n");
    serial_write_string("Growth mappings created: ");
    serial_write_u64(growth_mappings);
    serial_write_string("\n");
    serial_write_string("Growth PMM frames consumed: ");
    serial_write_u64(growth_pmm_frames);
    serial_write_string("\n");
    serial_write_string("Final mapped pages: ");
    serial_write_u64(heap_final.mapped_pages);
    serial_write_string("\n");
    serial_write_string("Final used bytes: ");
    serial_write_u64(heap_final.used_bytes);
    serial_write_string("\n");
    serial_write_string("Final free bytes: ");
    serial_write_u64(heap_final.free_bytes);
    serial_write_string("\n");

    return true;
}

#if BORING_TEST_MODE == BORING_TEST_MODE_NORMAL
struct cooperative_task_result {
    uint64_t id;
    uint64_t iterations;
    uintptr_t local_address;
    bool local_state_ok;
    bool stack_ok;
    bool register_ok;
    bool timer_ok;
};

static uint64_t task_test_sequence[TASK_TEST_SEQUENCE_LENGTH];
static size_t task_test_sequence_count;
static bool task_test_sequence_valid;
static bool task_test_timer_progress_ok;

static void cooperative_task_test_fail(const char *check) __attribute__((noreturn));
static void cooperative_task_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Cooperative task self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool task_test_wait_for_timer_tick(void) {
    const uint64_t before = timer_ticks();
    uint64_t spins = 0ULL;

    if (!task_test_timer_progress_ok) {
        return false;
    }

    while ((timer_ticks() <= before) &&
           (spins < TASK_TEST_TIMER_SPIN_LIMIT)) {
        x86_64_pause();
        ++spins;
    }

    if (timer_ticks() <= before) {
        task_test_timer_progress_ok = false;
        return false;
    }

    return true;
}

static void cooperative_task_entry(void *arg) {
    struct cooperative_task_result *result =
        (struct cooperative_task_result *)arg;
    volatile uint64_t local_counter = 0ULL;
    const uintptr_t local_address = (uintptr_t)&local_counter;
    uint64_t iteration;

    if (result == NULL) {
        return;
    }

    result->local_state_ok = true;
    result->stack_ok = true;
    result->register_ok = true;
    result->timer_ok = true;
    result->local_address = local_address;

    if ((task_current_id() != result->id) ||
        !task_current_stack_contains((const void *)&local_counter)) {
        result->stack_ok = false;
    }

    for (iteration = 0ULL; iteration < TASK_TEST_ITERATIONS; ++iteration) {
        if (local_counter != iteration) {
            result->local_state_ok = false;
        }
        local_counter = iteration + 1ULL;
        result->iterations = iteration + 1ULL;

        if (task_test_sequence_count >=
            (size_t)TASK_TEST_SEQUENCE_LENGTH) {
            task_test_sequence_valid = false;
        } else {
            task_test_sequence[task_test_sequence_count] = result->id;
            ++task_test_sequence_count;
        }

        if (!task_test_wait_for_timer_tick()) {
            result->timer_ok = false;
        }

        if ((iteration + 1ULL) < TASK_TEST_ITERATIONS) {
            if (iteration == 0ULL) {
                if (!x86_64_context_test_callee_saved(task_yield)) {
                    result->register_ok = false;
                }
            } else {
                task_yield();
            }

            if ((local_counter != (iteration + 1ULL)) ||
                (task_current_id() != result->id)) {
                result->local_state_ok = false;
            }
            if (!task_current_stack_contains(
                    (const void *)&local_counter) ||
                ((uintptr_t)&local_counter != local_address)) {
                result->stack_ok = false;
            }
        }
    }
}

static void hardware_irq_test_fail(const char *check) __attribute__((noreturn));
static void hardware_irq_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Hardware interrupt self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void run_hardware_irq_test(void) {
    struct irq_stats irq_before;
    struct irq_stats irq_after;
    struct timer_stats timer_stats;
    uint64_t start;
    uint64_t target;
    uint64_t observed;
    uint64_t spins = 0ULL;

    if (!irq_init() || !irq_get_stats(&irq_before) ||
        (irq_before.master_mask != 0xffU) ||
        (irq_before.slave_mask != 0xffU)) {
        serial_write_string("Hardware interrupts: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Hardware interrupts:\n");
    serial_write_string("Controller: 8259 PIC\n");
    serial_write_string("PIC: remapped\n");
    serial_write_string("Master vectors: 32-39\n");
    serial_write_string("Slave vectors: 40-47\n");
    serial_write_string("Initial master mask: 255\n");
    serial_write_string("Initial slave mask: 255\n");
    serial_write_string("IRQ0 vector: 32\n");

    if (!timer_init(BOOTSTRAP_TIMER_FREQUENCY_HZ) ||
        !timer_get_stats(&timer_stats) ||
        !irq_get_stats(&irq_before) ||
        (timer_ticks() != 0ULL) ||
        (irq_before.master_mask != 0xfeU) ||
        (irq_before.slave_mask != 0xffU)) {
        serial_write_string("Timer: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Master mask: 254\n");
    serial_write_string("Slave mask: 255\n\n");
    serial_write_string("Timer:\n");
    serial_write_string("Source: PIT channel 0\n");
    serial_write_string("Input frequency: ");
    serial_write_u64((uint64_t)timer_stats.input_frequency_hz);
    serial_write_string(" Hz\nRequested frequency: ");
    serial_write_u64((uint64_t)timer_stats.requested_frequency_hz);
    serial_write_string(" Hz\nDivisor: ");
    serial_write_u64((uint64_t)timer_stats.divisor);
    serial_write_string("\nEffective frequency: ");
    serial_write_u64((uint64_t)timer_stats.effective_frequency_millihz);
    serial_write_string(" mHz\nIRQ: 0\nVector: 32\nTimer: online\n\n");

    start = timer_ticks();
    if (start != 0ULL) {
        hardware_irq_test_fail("known-state");
    }

    if (!irq_enable()) {
        hardware_irq_test_fail("interrupt-enable");
    }
    serial_write_string("Interrupts: enabled\n\n");

    target = start + IRQ_TEST_TICKS;
    while ((timer_ticks() < target) && (spins < IRQ_TEST_SPIN_LIMIT)) {
        x86_64_pause();
        ++spins;
    }
    observed = timer_ticks();

    serial_write_string("IRQ self-test:\n");
    if (observed <= start) {
        hardware_irq_test_fail("timer-delivery");
    }
    serial_write_string("  timer-delivery: PASS\n");

    if (observed < target) {
        hardware_irq_test_fail("repeated-irqs");
    }
    serial_write_string("  repeated-irqs: PASS\n");

    if (!irq_get_stats(&irq_after) ||
        (irq_after.timer_irq_count < IRQ_TEST_TICKS) ||
        (irq_after.unexpected_irq_count != 0ULL)) {
        hardware_irq_test_fail("acknowledgement");
    }
    serial_write_string("  acknowledgement: PASS\n");

    serial_write_string("Ticks observed: ");
    serial_write_u64(observed);
    serial_write_string("\nIRQ0 deliveries: ");
    serial_write_u64(irq_after.timer_irq_count);
    serial_write_string("\nUnexpected IRQs: ");
    serial_write_u64(irq_after.unexpected_irq_count);
    serial_write_string("\nSpurious IRQ7: ");
    serial_write_u64(irq_after.spurious_irq7_count);
    serial_write_string("\nSpurious IRQ15: ");
    serial_write_u64(irq_after.spurious_irq15_count);
    serial_write_string("\n\nBoringKernel hardware interrupt test passed.\n\n");
}

static void run_cooperative_task_test(void) {
    struct cooperative_task_result task_a = { 0 };
    struct cooperative_task_result task_b = { 0 };
    struct task_stats task_stats_before;
    struct task_stats task_stats_after;
    struct task_stats task_stats_cleanup;
    struct heap_stats heap_before;
    struct heap_stats heap_after;
    uint64_t ticks_before;
    uint64_t ticks_after;
    uint64_t freed_stacks = 0ULL;
    bool alternating = true;
    size_t index;

    task_test_sequence_count = 0U;
    task_test_sequence_valid = true;
    task_test_timer_progress_ok = true;
    for (index = 0U; index < (size_t)TASK_TEST_SEQUENCE_LENGTH; ++index) {
        task_test_sequence[index] = 0ULL;
    }

    if (!heap_get_stats(&heap_before) || !task_init()) {
        serial_write_string("Kernel tasks: FAILED\n");
        x86_64_halt_forever();
    }

    if (!task_create(cooperative_task_entry, &task_a, &task_a.id) ||
        !task_create(cooperative_task_entry, &task_b, &task_b.id) ||
        !task_get_stats(&task_stats_before) ||
        (task_stats_before.created_tasks != 2ULL) ||
        (task_stats_before.active_tasks != 2ULL) ||
        (task_stats_before.current_task_id != KERNEL_BOOTSTRAP_TASK_ID) ||
        (task_stats_before.stack_size !=
         (size_t)KERNEL_TASK_STACK_SIZE)) {
        serial_write_string("Kernel tasks: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Kernel tasks:\n");
    serial_write_string("Mode: cooperative\n");
    serial_write_string("Tasks created: 2\n");
    serial_write_string("Task stack size: 16384 bytes\n");
    serial_write_string("Bootstrap task ID: 0\n");
    serial_write_string("Scheduler: online\n\n");

    ticks_before = timer_ticks();
    task_yield();
    ticks_after = timer_ticks();

    if (!task_get_stats(&task_stats_after)) {
        cooperative_task_test_fail("task-return");
    }

    if ((!task_test_sequence_valid) ||
        (task_test_sequence_count !=
         (size_t)TASK_TEST_SEQUENCE_LENGTH)) {
        alternating = false;
    } else {
        for (index = 0U; index < (size_t)TASK_TEST_SEQUENCE_LENGTH; ++index) {
            const uint64_t expected =
                ((index & 1U) == 0U) ? task_a.id : task_b.id;
            if (task_test_sequence[index] != expected) {
                alternating = false;
            }
        }
    }

    serial_write_string("Task A:\n  iterations: ");
    serial_write_u64(task_a.iterations);
    serial_write_string("\n");
    if ((task_a.iterations != TASK_TEST_ITERATIONS) ||
        !task_a.local_state_ok) {
        cooperative_task_test_fail("task-a-local-state");
    }
    serial_write_string("  local-state: PASS\n\n");

    serial_write_string("Task B:\n  iterations: ");
    serial_write_u64(task_b.iterations);
    serial_write_string("\n");
    if ((task_b.iterations != TASK_TEST_ITERATIONS) ||
        !task_b.local_state_ok) {
        cooperative_task_test_fail("task-b-local-state");
    }
    serial_write_string("  local-state: PASS\n\n");

    serial_write_string("Context switch self-test:\n");
    if ((task_a.iterations == 0ULL) || (task_a.id == 0ULL)) {
        cooperative_task_test_fail("task-a-start");
    }
    serial_write_string("  task-a-start: PASS\n");

    if ((task_b.iterations == 0ULL) || (task_b.id == 0ULL) ||
        (task_b.id == task_a.id)) {
        cooperative_task_test_fail("task-b-start");
    }
    serial_write_string("  task-b-start: PASS\n");

    if (!alternating) {
        cooperative_task_test_fail("alternating-switch");
    }
    serial_write_string("  alternating-switch: PASS\n");

    if ((!task_a.stack_ok) || (!task_b.stack_ok) ||
        (task_a.local_address == 0U) || (task_b.local_address == 0U) ||
        (task_a.local_address == task_b.local_address)) {
        cooperative_task_test_fail("stack-isolation");
    }
    serial_write_string("  stack-isolation: PASS\n");

    if ((!task_a.register_ok) || (!task_b.register_ok)) {
        cooperative_task_test_fail("register-state");
    }
    serial_write_string("  register-state: PASS\n");

    if ((task_stats_after.finished_tasks != 2ULL) ||
        (task_stats_after.created_tasks != 2ULL) ||
        (task_stats_after.active_tasks != 2ULL) ||
        (task_stats_after.current_task_id != KERNEL_BOOTSTRAP_TASK_ID)) {
        cooperative_task_test_fail("task-return");
    }
    serial_write_string("  task-return: PASS\n");

    if ((!task_a.timer_ok) || (!task_b.timer_ok) ||
        (!task_test_timer_progress_ok) || (ticks_after <= ticks_before)) {
        cooperative_task_test_fail("timer-coexistence");
    }
    serial_write_string("  timer-coexistence: PASS\n");

    if (!task_cleanup_finished(&freed_stacks) ||
        (freed_stacks != 2ULL) ||
        !task_get_stats(&task_stats_cleanup) ||
        (task_stats_cleanup.active_tasks != 0ULL)) {
        cooperative_task_test_fail("stack-cleanup");
    }
    serial_write_string("  stack-cleanup: PASS\n");

    if (!heap_get_stats(&heap_after) ||
        (heap_after.allocation_count != heap_before.allocation_count) ||
        (heap_after.used_bytes != heap_before.used_bytes) ||
        (heap_after.mapped_pages < heap_before.mapped_pages)) {
        cooperative_task_test_fail("heap-bookkeeping");
    }
    serial_write_string("  heap-bookkeeping: PASS\n");

    serial_write_string("Context switches: ");
    serial_write_u64(task_stats_after.context_switches);
    serial_write_string("\nTicks before task test: ");
    serial_write_u64(ticks_before);
    serial_write_string("\nTicks after task test: ");
    serial_write_u64(ticks_after);
    serial_write_string("\nTask stacks freed: ");
    serial_write_u64(freed_stacks);
    serial_write_string("\nTask heap allocations after cleanup: ");
    serial_write_u64(heap_after.allocation_count);
    serial_write_string("\n\nBoringKernel cooperative task test passed.\n");
}
#else
static void run_exception_test_mode(void) __attribute__((noreturn));
static void run_exception_test_mode(void) {
#if BORING_TEST_MODE == BORING_TEST_MODE_DIVIDE
    serial_write_string("Exception test mode: divide\n");
    serial_write_string("Triggering real Divide Error.\n");
    x86_64_trigger_divide_error();
#elif BORING_TEST_MODE == BORING_TEST_MODE_PAGEFAULT
    const uintptr_t fault_address = vmm_test_virtual_address();
    uint64_t translated = 0ULL;

    if (vmm_translate(fault_address, &translated)) {
        serial_write_string("Page Fault test setup FAILED: address is mapped\n");
        x86_64_halt_forever();
    }

    serial_write_string("Exception test mode: pagefault\n");
    serial_write_string("Expected CR2: ");
    serial_write_hex_u64((uint64_t)fault_address);
    serial_write_string("\nTriggering real Page Fault.\n");
    x86_64_trigger_page_fault(fault_address);
#elif BORING_TEST_MODE == BORING_TEST_MODE_RING3
    ring3_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_SYSCALL
    syscall_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_RUNTIME
    syscall_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_CONSOLE
    syscall_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_VFS
    vfs_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_RAMFS
    ramfs_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_INIT
    init_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_SHELL
    shell_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_BLOCK
    block_device_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_VIRTIO_BLOCK
    virtio_blk_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_BORINGFS_RO
    boringfs_ro_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_BORINGFS_RW
    boringfs_ro_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_PERSISTENT_ROOT
    boringfs_ro_test_run();
#elif BORING_TEST_MODE == BORING_TEST_MODE_M33_IPC
    ipc_test_run(elf_boot_module_response());
#endif
}
#endif

void boring_kernel_entry(void) {
    struct pmm_stats pmm_stats;
    struct pmm_stats pmm_before_heap;
    struct pmm_stats pmm_after_heap;
    struct vmm_stats vmm_stats;
    struct vmm_stats vmm_before_heap;
    struct vmm_stats vmm_after_heap;
    struct heap_stats heap_stats;
    struct exception_stats exception_stats;
    enum boring_framebuffer_status framebuffer_status;
    const struct boring_framebuffer *framebuffer_surface;
    uint64_t heap_init_pmm_frames;
    uint64_t heap_init_page_table_frames;

    if (!BORING_LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        x86_64_halt_forever();
    }

    serial_init();
    serial_write_string("BoringOS booting...\n");
    serial_write_string("BoringKernel 0.0.49-dev\n");
    serial_write_string("Arch: x86_64\n");
    serial_write_string("Hello from BoringKernel.\n\n");

    boring_cpu_inventory_init();
    boring_pci_inventory_init();
    boring_smbios_boot_init(limine_hhdm_request.response,
                            limine_memmap_request.response);

    framebuffer_status = boring_framebuffer_boot_init();
    framebuffer_surface = boring_framebuffer_get();
    if ((framebuffer_status == BORING_FRAMEBUFFER_STATUS_READY) &&
        (framebuffer_surface != NULL)) {
        serial_write_string("boring-framebuffer: detected\n");
        serial_write_string("boring-framebuffer: ");
        serial_write_u64(framebuffer_surface->width);
        serial_write_string("x");
        serial_write_u64(framebuffer_surface->height);
        serial_write_string("x");
        serial_write_u64((uint64_t)framebuffer_surface->bpp);
        serial_write_string("\nboring-framebuffer: pitch ");
        serial_write_u64(framebuffer_surface->pitch);
        serial_write_string("\nboring-framebuffer: rgb validated\n");
        serial_write_string("boring-graphics: primitives online\n");
        serial_write_string("boring-graphics: pixel font online\n\n");
    } else if (framebuffer_status == BORING_FRAMEBUFFER_STATUS_UNAVAILABLE) {
        serial_write_string("boring-framebuffer: unavailable, serial-only boot\n\n");
    } else if (framebuffer_status == BORING_FRAMEBUFFER_STATUS_UNSUPPORTED) {
        serial_write_string("boring-framebuffer: unsupported, serial-only boot\n\n");
    } else {
        serial_write_string("boring-framebuffer: malformed, serial-only boot\n\n");
    }

    if (!pmm_init(limine_memmap_request.response) ||
        !pmm_get_stats(&pmm_stats)) {
        serial_write_string("Physical memory manager: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Physical memory manager:\n");
    serial_write_string("Page size: 4096 bytes\n");
    serial_write_string("Usable memory: ");
    serial_write_u64(pmm_stats.usable_bytes);
    serial_write_string(" bytes\n");
    serial_write_string("Usable frames: ");
    serial_write_u64(pmm_stats.usable_frames);
    serial_write_string("\n");
    serial_write_string("Usable regions: ");
    serial_write_u64(pmm_stats.region_count);
    serial_write_string("\n");
    serial_write_string("Memory map capped: ");
    serial_write_string(pmm_stats.memory_map_capped ? "yes" : "no");
    serial_write_string("\n");
    serial_write_string("PMM: online\n\n");

    if (!pmm_self_test()) {
        x86_64_halt_forever();
    }
    serial_write_string("\nBoringKernel physical memory test passed.\n\n");

    if (!vmm_init(limine_hhdm_request.response,
                  limine_paging_mode_request.response,
                  limine_memmap_request.response) ||
        !vmm_get_stats(&vmm_stats)) {
        serial_write_string("Virtual memory manager: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Virtual memory manager:\n");
    serial_write_string("Paging: x86_64 4-level\n");
    serial_write_string("Page size: 4096 bytes\n");
    serial_write_string("Active root table: ");
    serial_write_u64(vmm_stats.active_root_physical);
    serial_write_string("\n");
    serial_write_string("HHDM offset: ");
    serial_write_u64(vmm_stats.hhdm_offset);
    serial_write_string("\n");
    serial_write_string("Test virtual address: ");
    serial_write_u64((uint64_t)vmm_test_virtual_address());
    serial_write_string("\n");
    serial_write_string("VMM: online\n\n");

    if (!vmm_self_test()) {
        x86_64_halt_forever();
    }
    serial_write_string("\nBoringKernel virtual memory test passed.\n\n");

    if (!pmm_get_stats(&pmm_before_heap) ||
        !vmm_get_stats(&vmm_before_heap) ||
        !heap_init() ||
        !pmm_get_stats(&pmm_after_heap) ||
        !vmm_get_stats(&vmm_after_heap) ||
        !heap_get_stats(&heap_stats) ||
        (pmm_before_heap.free_frames < pmm_after_heap.free_frames) ||
        (vmm_after_heap.owned_page_table_frames <
         vmm_before_heap.owned_page_table_frames)) {
        serial_write_string("Kernel heap: FAILED\n");
        x86_64_halt_forever();
    }

    heap_init_pmm_frames = pmm_before_heap.free_frames -
                           pmm_after_heap.free_frames;
    heap_init_page_table_frames =
        vmm_after_heap.owned_page_table_frames -
        vmm_before_heap.owned_page_table_frames;

    serial_write_string("Kernel heap:\n");
    serial_write_string("Virtual base: ");
    serial_write_u64((uint64_t)heap_stats.virtual_base);
    serial_write_string("\n");
    serial_write_string("Virtual limit: ");
    serial_write_u64((uint64_t)heap_stats.virtual_limit);
    serial_write_string("\n");
    serial_write_string("Mapped pages: ");
    serial_write_u64(heap_stats.mapped_pages);
    serial_write_string("\n");
    serial_write_string("Mapped capacity: ");
    serial_write_u64(heap_stats.mapped_bytes);
    serial_write_string(" bytes\n");
    serial_write_string("Free payload: ");
    serial_write_u64(heap_stats.free_bytes);
    serial_write_string(" bytes\n");
    serial_write_string("Alignment: 16 bytes\n");
    serial_write_string("Initial PMM frames consumed: ");
    serial_write_u64(heap_init_pmm_frames);
    serial_write_string("\n");
    serial_write_string("Initial page-table frames: ");
    serial_write_u64(heap_init_page_table_frames);
    serial_write_string("\n");
    serial_write_string("Heap: online\n\n");

    if (!heap_self_test()) {
        x86_64_halt_forever();
    }
    serial_write_string("\nBoringKernel heap test passed.\n\n");

    if (!exception_init() || !exception_get_stats(&exception_stats) ||
        (exception_stats.configured_vectors !=
         (uint16_t)X86_64_EXCEPTION_VECTOR_COUNT)) {
        serial_write_string("Exception handling: FAILED\n");
        x86_64_halt_forever();
    }

    serial_write_string("Exception handling:\n");
    serial_write_string("IDT entries: 32\n");
    serial_write_string("IDTR base: ");
    serial_write_hex_u64((uint64_t)exception_stats.idtr_base);
    serial_write_string("\nIDTR limit: ");
    serial_write_u64((uint64_t)exception_stats.idtr_limit);
    serial_write_string("\nCode selector: ");
    serial_write_hex_u64((uint64_t)exception_stats.code_selector);
    serial_write_string("\nIDT: loaded\n");
    serial_write_string("Exceptions: online\n\n");
    serial_write_string("BoringKernel exception infrastructure test passed.\n\n");

#if BORING_TEST_MODE == BORING_TEST_MODE_NORMAL
    run_hardware_irq_test();
    if (!process_init()) {
        serial_write_string("Process subsystem: FAILED\n");
        x86_64_halt_forever();
    }
    if (framebuffer_surface != NULL) {
        const struct boring_boot_dashboard_info dashboard_info = {
            .kernel_name = "BoringKernel",
            .kernel_version = "0.0.49-dev",
            .arch = "x86_64",
            .memory_bytes = pmm_stats.usable_bytes,
            .root_fs = "N/A",
            .block_device = "N/A",
            .pmm_online = true,
            .vmm_online = true,
            .irq_online = true,
            .ring3_available = true,
            .vfs_online = false,
            .boringfs_online = false
        };
        if (boring_boot_dashboard_render(framebuffer_surface, &dashboard_info)) {
            serial_write_string("boring-graphics: dashboard rendered\n\n");
        } else {
            serial_write_string("boring-graphics: dashboard skipped\n\n");
        }
    }
    run_cooperative_task_test();
    run_preemptive_task_test();
    run_process_address_space_test();
    x86_64_halt_forever();
#else
    run_exception_test_mode();
#endif
}
