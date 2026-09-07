#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/framebuffer.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define TEST_ARENA_PAGES 96U
#define TEST_ALLOC_FIRST_PAGE 16U
#define TEST_PRESENT (1ULL << 0)
#define TEST_WRITABLE (1ULL << 1)
#define TEST_USER (1ULL << 2)
#define TEST_PWT (1ULL << 3)
#define TEST_PCD (1ULL << 4)
#define TEST_LARGE (1ULL << 7)
#define TEST_PAT_4K (1ULL << 7)
#define TEST_NX (1ULL << 63)
#define TEST_ADDRESS_MASK 0x000ffffffffff000ULL
#define TEST_FB_PHYSICAL 0x30000080ULL
#define TEST_FB_LENGTH 0x01000000ULL

static _Alignas(VMM_PAGE_SIZE)
    uint8_t test_arena[TEST_ARENA_PAGES * (size_t)VMM_PAGE_SIZE];
static bool test_allocated[TEST_ARENA_PAGES];
static uint64_t test_cr3;
static size_t test_alloc_calls;
static size_t test_fail_alloc_call = SIZE_MAX;
static size_t test_invalidations;
static size_t test_non_pmm_usable_queries;
static bool test_failed;

static uint64_t *test_table(size_t page) {
    return (uint64_t *)&test_arena[page * (size_t)VMM_PAGE_SIZE];
}

static uint64_t test_table_physical(size_t page) {
    return (uint64_t)(uintptr_t)test_table(page);
}

static size_t test_index(uintptr_t address, unsigned int shift) {
    return (size_t)(((uint64_t)address >> shift) & 0x1ffULL);
}

static void test_check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "vmm-framebuffer-host-test: FAIL: %s\n", name);
        test_failed = true;
    }
}

static bool test_arena_page(uint64_t physical_address, size_t *page) {
    const uintptr_t base = (uintptr_t)&test_arena[0];
    const uintptr_t address = (uintptr_t)physical_address;
    const uintptr_t size = (uintptr_t)sizeof(test_arena);

    if ((address < base) || (address >= (base + size)) ||
        (((address - base) & ((uintptr_t)VMM_PAGE_SIZE - 1U)) != 0U)) {
        return false;
    }
    if (page != NULL) {
        *page = (size_t)((address - base) / (uintptr_t)VMM_PAGE_SIZE);
    }
    return true;
}

bool pmm_alloc_frame(uint64_t *physical_address) {
    size_t page;

    if (physical_address == NULL) {
        return false;
    }
    ++test_alloc_calls;
    if (test_alloc_calls == test_fail_alloc_call) {
        return false;
    }
    for (page = TEST_ALLOC_FIRST_PAGE; page < TEST_ARENA_PAGES; ++page) {
        if (!test_allocated[page]) {
            test_allocated[page] = true;
            *physical_address = test_table_physical(page);
            return true;
        }
    }
    return false;
}

bool pmm_free_frame(uint64_t physical_address) {
    size_t page;

    if (!test_arena_page(physical_address, &page) ||
        (page < TEST_ALLOC_FIRST_PAGE) || !test_allocated[page]) {
        return false;
    }
    test_allocated[page] = false;
    return true;
}

bool pmm_frame_is_usable(uint64_t physical_address) {
    if (!test_arena_page(physical_address, NULL)) {
        ++test_non_pmm_usable_queries;
        return false;
    }
    return true;
}

uint64_t x86_64_read_cr3(void) {
    return test_cr3;
}

void x86_64_invalidate_page(uintptr_t virtual_address) {
    (void)virtual_address;
    ++test_invalidations;
}

static size_t test_outstanding_frames(void) {
    size_t page;
    size_t count = 0U;

    for (page = TEST_ALLOC_FIRST_PAGE; page < TEST_ARENA_PAGES; ++page) {
        if (test_allocated[page]) {
            ++count;
        }
    }
    return count;
}

static void test_zero_arena(void) {
    size_t index;

    for (index = 0U; index < sizeof(test_arena); ++index) {
        test_arena[index] = 0U;
    }
    for (index = 0U; index < TEST_ARENA_PAGES; ++index) {
        test_allocated[index] = false;
    }
}

static bool test_initialize_vmm(void) {
    static struct boring_limine_memmap_entry arena_entry;
    static struct boring_limine_memmap_entry framebuffer_entry;
    static struct boring_limine_memmap_entry second_framebuffer_entry;
    static struct boring_limine_memmap_entry *entries[3];
    static struct boring_limine_memmap_response memory_map;
    static struct boring_limine_hhdm_response hhdm;
    static struct boring_limine_paging_mode_response paging;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    const uintptr_t page4k = (uintptr_t)0x00100123ULL;
    const uintptr_t leaf_read_only = (uintptr_t)0x00200456ULL;
    const uintptr_t parent_read_only = (uintptr_t)0x00400789ULL;

    test_zero_arena();
    pml4 = test_table(0U);
    pdpt = test_table(1U);
    pd = test_table(2U);
    test_cr3 = test_table_physical(0U);

    pml4[0] = test_table_physical(1U) |
        TEST_PRESENT | TEST_WRITABLE | TEST_USER;
    pdpt[0] = test_table_physical(2U) |
        TEST_PRESENT | TEST_WRITABLE | TEST_USER;

    pd[test_index(page4k, 21U)] = test_table_physical(3U) |
        TEST_PRESENT | TEST_WRITABLE | TEST_USER;
    test_table(3U)[test_index(page4k, 12U)] = 0x10000000ULL |
        TEST_PRESENT | TEST_WRITABLE | TEST_USER | TEST_PWT | TEST_PCD |
        TEST_PAT_4K | TEST_NX;

    pd[test_index(leaf_read_only, 21U)] = test_table_physical(4U) |
        TEST_PRESENT | TEST_WRITABLE | TEST_USER;
    test_table(4U)[test_index(leaf_read_only, 12U)] = 0x11000000ULL |
        TEST_PRESENT | TEST_USER;

    pd[test_index(parent_read_only, 21U)] = test_table_physical(5U) |
        TEST_PRESENT | TEST_USER;
    test_table(5U)[test_index(parent_read_only, 12U)] = 0x12000000ULL |
        TEST_PRESENT | TEST_WRITABLE | TEST_USER;

    pd[test_index((uintptr_t)0x00800000ULL, 21U)] =
        test_table_physical(6U) | TEST_PRESENT | TEST_WRITABLE;

    pd[test_index((uintptr_t)TEST_FB_PHYSICAL, 21U)] =
        0x30000000ULL | TEST_PRESENT | TEST_WRITABLE | TEST_LARGE | TEST_PWT;

    pdpt[1U] = 0x80000000ULL |
        TEST_PRESENT | TEST_WRITABLE | TEST_LARGE;

    arena_entry.base = (uint64_t)(uintptr_t)&test_arena[0];
    arena_entry.length = (uint64_t)sizeof(test_arena);
    arena_entry.type = BORING_LIMINE_MEMMAP_USABLE;
    framebuffer_entry.base = TEST_FB_PHYSICAL;
    framebuffer_entry.length = TEST_FB_LENGTH;
    framebuffer_entry.type = BORING_LIMINE_MEMMAP_FRAMEBUFFER;
    second_framebuffer_entry.base = 0x50000000ULL;
    second_framebuffer_entry.length = 0x10000ULL;
    second_framebuffer_entry.type = BORING_LIMINE_MEMMAP_FRAMEBUFFER;
    entries[0] = &arena_entry;
    entries[1] = &framebuffer_entry;
    entries[2] = &second_framebuffer_entry;
    memory_map.revision = 0ULL;
    memory_map.entry_count = 3ULL;
    memory_map.entries = entries;
    hhdm.revision = 0ULL;
    hhdm.offset = 0ULL;
    paging.revision = 0ULL;
    paging.mode = BORING_LIMINE_PAGING_MODE_X86_64_4LVL;
    return vmm_init(&hhdm, &paging, &memory_map);
}

static void test_inspector(void) {
    struct vmm_mapping_info info;
    const uintptr_t page4k = (uintptr_t)0x00100123ULL;
    const uintptr_t page2m = (uintptr_t)0x30001234ULL;
    const uintptr_t page1g = (uintptr_t)0x41234567ULL;

    test_check(vmm_inspect_mapping(page4k, &info) && info.canonical &&
               info.present && (info.leaf_size == VMM_PAGE_SIZE) &&
               (info.physical_address == 0x10000123ULL) &&
               info.parent_writable && info.leaf_writable &&
               info.effective_writable && info.effective_user &&
               info.leaf_pwt && info.leaf_pcd && info.leaf_pat &&
               info.effective_nx,
               "4K leaf and flags");
    test_check(vmm_inspect_mapping(page2m, &info) && info.present &&
               (info.leaf_size == (1ULL << 21)) &&
               (info.physical_address == 0x30001234ULL) &&
               info.effective_writable && info.leaf_pwt,
               "2M leaf offset");
    test_check(vmm_inspect_mapping(page1g, &info) && info.present &&
               (info.leaf_size == (1ULL << 30)) &&
               (info.physical_address == 0x81234567ULL) &&
               info.effective_writable,
               "1G leaf offset");

    test_check(vmm_inspect_mapping((uintptr_t)0x00200456ULL, &info) &&
               info.present && info.parent_writable &&
               !info.leaf_writable && !info.effective_writable,
               "read-only leaf");
    test_check(vmm_inspect_mapping((uintptr_t)0x00400789ULL, &info) &&
               info.present && !info.parent_writable &&
               info.leaf_writable && !info.effective_writable,
               "read-only parent");
    test_check(vmm_inspect_mapping((uintptr_t)0x0000008000000000ULL,
                                   &info) &&
               info.canonical && !info.present &&
               (info.missing_level ==
                (uint8_t)VMM_PAGE_TABLE_LEVEL_PML4),
               "absent PML4");
    test_check(vmm_inspect_mapping((uintptr_t)0x80000000ULL, &info) &&
               info.canonical && !info.present &&
               (info.missing_level ==
                (uint8_t)VMM_PAGE_TABLE_LEVEL_PDPT),
               "absent PDPT");
    test_check(vmm_inspect_mapping((uintptr_t)0x00600000ULL, &info) &&
               !info.present &&
               (info.missing_level == (uint8_t)VMM_PAGE_TABLE_LEVEL_PD),
               "absent PD");
    test_check(vmm_inspect_mapping((uintptr_t)0x00800000ULL, &info) &&
               !info.present &&
               (info.missing_level == (uint8_t)VMM_PAGE_TABLE_LEVEL_PT),
               "absent PT leaf");
    test_check(vmm_inspect_mapping((uintptr_t)0x0000800000000000ULL,
                                   &info) &&
               !info.canonical && !info.present,
               "noncanonical address");
}

static void test_framebuffer_resolution(void) {
    struct vmm_framebuffer_resolution resolution;

    test_check(vmm_resolve_limine_framebuffer(
                   (uintptr_t)TEST_FB_PHYSICAL, 0x5000U, &resolution) &&
               (resolution.physical_base == TEST_FB_PHYSICAL) &&
               (resolution.physical_end == TEST_FB_PHYSICAL + 0x4fffULL) &&
               (resolution.memmap_base == TEST_FB_PHYSICAL) &&
               (resolution.memmap_length == TEST_FB_LENGTH) &&
               resolution.start_mapping.present &&
               resolution.end_mapping.present &&
               resolution.inherited_contiguous &&
               resolution.inherited_effective_writable,
               "validated framebuffer start/end and continuity");
    test_check(vmm_resolve_limine_framebuffer(
                   (uintptr_t)0x50000000ULL, 0x1000U, &resolution) &&
               !resolution.inherited_contiguous &&
               !resolution.inherited_effective_writable,
               "stale inherited alias classification");
    test_check(!vmm_resolve_limine_framebuffer(
                   (uintptr_t)0x60000000ULL, 0x1000U, &resolution),
               "unrelated physical range rejected");
    test_check(!vmm_resolve_limine_framebuffer(
                   UINTPTR_MAX - (uintptr_t)31U, 64U, &resolution),
               "virtual end overflow rejected");
}

static void test_page_spans(void) {
    size_t pages = 0U;

    test_check(vmm_framebuffer_mapping_page_count(
                   0x20000000ULL, 1U, &pages) && (pages == 1U),
               "aligned one-byte span");
    test_check(vmm_framebuffer_mapping_page_count(
                   0x20000001ULL, 4095U, &pages) && (pages == 1U),
               "exact page boundary span");
    test_check(vmm_framebuffer_mapping_page_count(
                   0x20000001ULL, 4096U, &pages) && (pages == 2U),
               "nonaligned multi-page span");
    test_check(vmm_framebuffer_mapping_page_count(
                   0x20000000ULL, 4097U, &pages) && (pages == 2U),
               "final partial page span");
    test_check(!vmm_framebuffer_mapping_page_count(
                   UINT64_MAX - 31ULL, 64U, &pages),
               "physical end overflow rejected");
}

static void test_mapping_and_rebind(void) {
    struct boring_framebuffer surface;
    struct boring_framebuffer original;
    struct boring_framebuffer failed_surface;
    struct boring_framebuffer failed_original;
    struct boring_m61_framebuffer_mapping_diagnostics diagnostics;
    struct vmm_mapping_info mapping;
    struct vmm_stats stats;
    volatile void *alias = NULL;
    uint64_t collision_frame = 0ULL;
    size_t pages = 0U;
    const size_t calls_before_failure = test_alloc_calls;

    test_check(!vmm_map_framebuffer_region(
                   TEST_FB_PHYSICAL, VMM_FRAMEBUFFER_WINDOW_SIZE + 1U,
                   &alias, &pages),
               "framebuffer window capacity enforced");

    test_check(pmm_alloc_frame(&collision_frame) &&
               vmm_map_page(VMM_FRAMEBUFFER_WINDOW_BASE, collision_frame,
                            VMM_FLAG_WRITABLE),
               "seed framebuffer-window collision");
    test_check(!vmm_map_framebuffer_region(
                   TEST_FB_PHYSICAL, 4096U, &alias, &pages),
               "existing window mapping collision rejected");
    test_check(vmm_unmap_page(VMM_FRAMEBUFFER_WINDOW_BASE) &&
               pmm_free_frame(collision_frame),
               "remove collision seed");

    test_fail_alloc_call = test_alloc_calls + 5U;
    test_check(!vmm_map_framebuffer_region(
                   TEST_FB_PHYSICAL, 12288U, &alias, &pages),
               "partial mapping failure rejected");
    test_fail_alloc_call = SIZE_MAX;
    test_check(vmm_inspect_mapping(VMM_FRAMEBUFFER_WINDOW_BASE, &mapping) &&
               !mapping.present && (test_outstanding_frames() == 0U),
               "partial mapping rollback and frame reclamation");
    test_check(test_alloc_calls > calls_before_failure,
               "partial mapping exercised allocator");

    test_check(boring_framebuffer_surface_init(
                   &surface, (volatile uint8_t *)(uintptr_t)TEST_FB_PHYSICAL,
                   16ULL, 3ULL, 4096ULL, 32U,
                   BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
                   8U, 16U, 8U, 8U, 8U, 0U),
               "framebuffer surface setup");
    original = surface;
    test_check(boring_m61_framebuffer_normalize(&surface, &diagnostics),
               "framebuffer normalization");
    test_check(diagnostics.alias_created &&
               diagnostics.memmap_range_match &&
               diagnostics.metadata_preserved &&
               (diagnostics.mapping_pages == 4U) &&
               (diagnostics.alias_virtual_start ==
                VMM_FRAMEBUFFER_WINDOW_BASE + 0x80U) &&
               diagnostics.alias_start_mapping.present &&
               diagnostics.alias_start_mapping.effective_writable &&
               diagnostics.alias_start_mapping.leaf_pcd &&
               (diagnostics.alias_start_mapping.physical_address ==
                TEST_FB_PHYSICAL) &&
               diagnostics.alias_end_mapping.present &&
               diagnostics.alias_end_mapping.effective_writable &&
               diagnostics.alias_end_mapping.leaf_pcd &&
               (diagnostics.alias_end_mapping.physical_address ==
                TEST_FB_PHYSICAL + surface.byte_size - 1ULL) &&
               (surface.address != original.address) &&
               (surface.width == original.width) &&
               (surface.height == original.height) &&
               (surface.pitch == original.pitch) &&
               (surface.byte_size == original.byte_size) &&
               (surface.bpp == original.bpp),
               "rebind address only after mapping success");
    test_check(vmm_inspect_mapping((uintptr_t)surface.address, &mapping) &&
               mapping.present && mapping.effective_writable &&
               mapping.leaf_pcd &&
               (mapping.physical_address == TEST_FB_PHYSICAL),
               "writable PCD framebuffer alias");
    test_check(vmm_get_stats(&stats) &&
               (test_outstanding_frames() ==
                (size_t)stats.owned_page_table_frames),
               "no staging frame leak");
    test_check(test_non_pmm_usable_queries == 0U,
               "framebuffer pages never treated as PMM RAM");
    test_check(test_invalidations != 0U,
               "page-table changes invalidate translations");

    test_check(boring_framebuffer_surface_init(
                   &failed_surface,
                   (volatile uint8_t *)(uintptr_t)0x50000000ULL,
                   1ULL, 1ULL, 4ULL, 32U,
                   BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
                   8U, 16U, 8U, 8U, 8U, 0U),
               "failed rebind surface setup");
    failed_original = failed_surface;
    test_check(!boring_m61_framebuffer_normalize(
                   &failed_surface, &diagnostics) &&
               (failed_surface.address == failed_original.address) &&
               (failed_surface.width == failed_original.width) &&
               (failed_surface.height == failed_original.height) &&
               (failed_surface.pitch == failed_original.pitch) &&
               (failed_surface.byte_size == failed_original.byte_size),
               "failed normalization preserves original surface");
}

int main(void) {
    test_check(test_initialize_vmm(), "VMM initialization");
    if (!test_failed) {
        test_inspector();
        test_framebuffer_resolution();
        test_page_spans();
        test_mapping_and_rebind();
    }
    if (test_failed) {
        return 1;
    }
    (void)puts("VMM framebuffer inspection/MMIO normalization host tests passed.");
    return 0;
}
