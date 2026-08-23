#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/elf_loader.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/vmm.h>

#define ELF_IDENT_SIZE 16U
#define ELF_EI_MAG0 0U
#define ELF_EI_MAG1 1U
#define ELF_EI_MAG2 2U
#define ELF_EI_MAG3 3U
#define ELF_EI_CLASS 4U
#define ELF_EI_DATA 5U
#define ELF_EI_VERSION 6U
#define ELFCLASS64 2U
#define ELFDATA2LSB 1U
#define EV_CURRENT 1U
#define ET_EXEC 2U
#define EM_X86_64 62U
#define PT_LOAD 1U
#define PT_DYNAMIC 2U
#define PT_INTERP 3U
#define ELF64_EHDR_SIZE 64U
#define ELF64_PHDR_SIZE 56U
#define ELF_SUPPORTED_FLAGS (BORING_ELF_PF_R | BORING_ELF_PF_W | BORING_ELF_PF_X)

struct boring_elf64_ehdr {
    uint8_t ident[ELF_IDENT_SIZE];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct boring_elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

_Static_assert(sizeof(struct boring_elf64_ehdr) == ELF64_EHDR_SIZE,
               "ELF64 header size mismatch");
_Static_assert(sizeof(struct boring_elf64_phdr) == ELF64_PHDR_SIZE,
               "ELF64 program header size mismatch");

static void byte_copy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;

    for (index = 0U; index < length; ++index) {
        out[index] = in[index];
    }
}

static bool range_inside(size_t total, uint64_t offset, uint64_t length) {
    uint64_t end;

    if ((offset > (uint64_t)total) || (length > UINT64_MAX - offset)) {
        return false;
    }
    end = offset + length;
    return end <= (uint64_t)total;
}

static bool read_object(const uint8_t *bytes,
                        size_t size,
                        uint64_t offset,
                        void *object,
                        size_t object_size) {
    if ((bytes == NULL) || (object == NULL) ||
        !range_inside(size, offset, (uint64_t)object_size)) {
        return false;
    }

    byte_copy(object, &bytes[(size_t)offset], object_size);
    return true;
}

static bool add_u64(uint64_t first, uint64_t second, uint64_t *result) {
    if ((result == NULL) || (second > UINT64_MAX - first)) {
        return false;
    }
    *result = first + second;
    return true;
}

static bool mul_u64(uint64_t first, uint64_t second, uint64_t *result) {
    if ((result == NULL) || ((first != 0ULL) &&
        (second > UINT64_MAX / first))) {
        return false;
    }
    *result = first * second;
    return true;
}

static bool page_count_for_size(uint64_t size, uint64_t *page_count) {
    uint64_t rounded;

    if ((page_count == NULL) || (size == 0ULL) ||
        (size > UINT64_MAX - (BORING_ELF_PAGE_SIZE - 1ULL))) {
        return false;
    }
    rounded = size + (BORING_ELF_PAGE_SIZE - 1ULL);
    *page_count = rounded / BORING_ELF_PAGE_SIZE;
    return *page_count != 0ULL;
}

static bool ranges_overlap(uint64_t first_start,
                           uint64_t first_end,
                           uint64_t second_start,
                           uint64_t second_end) {
    return (first_start < second_end) && (second_start < first_end);
}

static bool segment_page_end(const struct boring_elf_segment *segment,
                             uint64_t *end) {
    uint64_t bytes;

    return (segment != NULL) && (end != NULL) &&
           mul_u64(segment->page_count, BORING_ELF_PAGE_SIZE, &bytes) &&
           add_u64(segment->virtual_address, bytes, end);
}

static bool physical_page_pointer(uint64_t physical_address,
                                  uint8_t **pointer) {
    struct vmm_stats stats;
    uint64_t virtual_address;

    if ((pointer == NULL) ||
        ((physical_address & (VMM_PAGE_SIZE - 1ULL)) != 0ULL) ||
        !pmm_frame_is_usable(physical_address) ||
        !vmm_get_stats(&stats) ||
        (physical_address > UINT64_MAX - stats.hhdm_offset)) {
        return false;
    }

    virtual_address = stats.hhdm_offset + physical_address;
    if ((virtual_address >> 48U) != 0xffffULL) {
        return false;
    }

    *pointer = (uint8_t *)(uintptr_t)virtual_address;
    return true;
}

static void zero_page(uint8_t *page) {
    size_t index;

    for (index = 0U; index < (size_t)VMM_PAGE_SIZE; ++index) {
        page[index] = 0U;
    }
}

static void validation_clear(struct boring_elf_validation *validation) {
    size_t index;

    if (validation == NULL) {
        return;
    }

    validation->entry = 0U;
    validation->program_header_count = 0U;
    validation->load_segment_count = 0U;
    validation->total_image_pages = 0ULL;
    for (index = 0U; index < (size_t)BORING_ELF_MAX_LOAD_SEGMENTS; ++index) {
        validation->segments[index].file_offset = 0ULL;
        validation->segments[index].virtual_address = 0ULL;
        validation->segments[index].file_size = 0ULL;
        validation->segments[index].memory_size = 0ULL;
        validation->segments[index].page_count = 0ULL;
        validation->segments[index].flags = 0U;
    }
}

static void image_clear(struct boring_elf_image *image) {
    size_t index;

    if (image == NULL) {
        return;
    }

    image->process = NULL;
    image->entry = 0U;
    image->stack_base = 0U;
    image->stack_top = 0U;
    image->module_size = 0ULL;
    image->program_header_count = 0U;
    image->load_segment_count = 0U;
    image->owned_page_count = 0U;

    for (index = 0U; index < (size_t)BORING_ELF_MAX_LOAD_SEGMENTS; ++index) {
        image->segments[index].file_offset = 0ULL;
        image->segments[index].virtual_address = 0ULL;
        image->segments[index].file_size = 0ULL;
        image->segments[index].memory_size = 0ULL;
        image->segments[index].page_count = 0ULL;
        image->segments[index].flags = 0U;
    }

    for (index = 0U;
         index < (size_t)(BORING_ELF_MAX_IMAGE_PAGES + BORING_ELF_STACK_PAGES);
         ++index) {
        image->pages[index].virtual_address = 0U;
        image->pages[index].physical_address = 0ULL;
        image->pages[index].writable = false;
        image->pages[index].executable = false;
        image->pages[index].mapped = false;
    }
}

bool boring_elf_validate(const uint8_t *module_bytes,
                         size_t module_size,
                         uintptr_t stack_base,
                         size_t stack_size,
                         struct boring_elf_validation *validation) {
    struct boring_elf64_ehdr header;
    uint64_t program_table_bytes;
    uint64_t program_table_end;
    uint64_t stack_end;
    uint16_t index;
    bool entry_ok = false;

    validation_clear(validation);
    if ((module_bytes == NULL) || (validation == NULL) ||
        (module_size < (size_t)ELF64_EHDR_SIZE) ||
        (module_size > (size_t)BORING_ELF_MODULE_MAX_SIZE) ||
        ((stack_base & (uintptr_t)(BORING_ELF_PAGE_SIZE - 1ULL)) != 0U) ||
        (stack_size == 0U) ||
        (((uint64_t)stack_size & (BORING_ELF_PAGE_SIZE - 1ULL)) != 0ULL) ||
        !ring3_user_range_valid(stack_base, stack_size) ||
        !read_object(module_bytes, module_size, 0ULL,
                     &header, sizeof(header))) {
        return false;
    }

    if ((header.ident[ELF_EI_MAG0] != 0x7fU) ||
        (header.ident[ELF_EI_MAG1] != (uint8_t)'E') ||
        (header.ident[ELF_EI_MAG2] != (uint8_t)'L') ||
        (header.ident[ELF_EI_MAG3] != (uint8_t)'F') ||
        (header.ident[ELF_EI_CLASS] != ELFCLASS64) ||
        (header.ident[ELF_EI_DATA] != ELFDATA2LSB) ||
        (header.ident[ELF_EI_VERSION] != EV_CURRENT) ||
        (header.type != ET_EXEC) || (header.machine != EM_X86_64) ||
        (header.version != EV_CURRENT) ||
        (header.ehsize != (uint16_t)ELF64_EHDR_SIZE) ||
        (header.phentsize != (uint16_t)ELF64_PHDR_SIZE) ||
        (header.phnum == 0U) ||
        (header.phnum > (uint16_t)BORING_ELF_MAX_PROGRAM_HEADERS) ||
        !mul_u64((uint64_t)header.phnum, (uint64_t)header.phentsize,
                 &program_table_bytes) ||
        !add_u64(header.phoff, program_table_bytes, &program_table_end) ||
        (program_table_end > (uint64_t)module_size) ||
        !add_u64((uint64_t)stack_base, (uint64_t)stack_size, &stack_end)) {
        return false;
    }

    validation->entry = (uintptr_t)header.entry;
    validation->program_header_count = header.phnum;

    for (index = 0U; index < header.phnum; ++index) {
        struct boring_elf64_phdr program_header;
        uint64_t phdr_offset;
        uint64_t memory_end;
        uint64_t page_end;
        uint64_t page_count;
        uint16_t existing;

        if (!mul_u64((uint64_t)index, (uint64_t)header.phentsize,
                     &phdr_offset) ||
            !add_u64(header.phoff, phdr_offset, &phdr_offset) ||
            !read_object(module_bytes, module_size, phdr_offset,
                         &program_header, sizeof(program_header))) {
            return false;
        }

        if ((program_header.type == PT_INTERP) ||
            (program_header.type == PT_DYNAMIC)) {
            return false;
        }
        if (program_header.type != PT_LOAD) {
            continue;
        }

        if (validation->load_segment_count >=
            (uint16_t)BORING_ELF_MAX_LOAD_SEGMENTS) {
            return false;
        }
        if ((program_header.memsz == 0ULL) ||
            (program_header.filesz > program_header.memsz) ||
            ((program_header.flags & ~ELF_SUPPORTED_FLAGS) != 0U) ||
            ((program_header.flags & BORING_ELF_PF_R) == 0U) ||
            (((program_header.flags & BORING_ELF_PF_W) != 0U) &&
             ((program_header.flags & BORING_ELF_PF_X) != 0U)) ||
            (program_header.align != BORING_ELF_PAGE_SIZE) ||
            ((program_header.vaddr & (BORING_ELF_PAGE_SIZE - 1ULL)) != 0ULL) ||
            ((program_header.offset & (BORING_ELF_PAGE_SIZE - 1ULL)) != 0ULL) ||
            ((program_header.vaddr % program_header.align) !=
             (program_header.offset % program_header.align)) ||
            !range_inside(module_size, program_header.offset,
                          program_header.filesz) ||
            !add_u64(program_header.vaddr, program_header.memsz,
                     &memory_end) ||
            (program_header.memsz > (uint64_t)SIZE_MAX) ||
            !ring3_user_range_valid((uintptr_t)program_header.vaddr,
                                    (size_t)program_header.memsz) ||
            !page_count_for_size(program_header.memsz, &page_count) ||
            (page_count > (uint64_t)BORING_ELF_MAX_IMAGE_PAGES) ||
            (validation->total_image_pages >
             (uint64_t)BORING_ELF_MAX_IMAGE_PAGES - page_count)) {
            return false;
        }

        if (!mul_u64(page_count, BORING_ELF_PAGE_SIZE, &page_end) ||
            !add_u64(program_header.vaddr, page_end, &page_end) ||
            ranges_overlap(program_header.vaddr, page_end,
                           (uint64_t)stack_base, stack_end)) {
            return false;
        }

        for (existing = 0U;
             existing < validation->load_segment_count; ++existing) {
            uint64_t existing_end;

            if (!segment_page_end(&validation->segments[existing],
                                  &existing_end) ||
                ranges_overlap(program_header.vaddr, page_end,
                               validation->segments[existing].virtual_address,
                               existing_end)) {
                return false;
            }
        }

        validation->segments[validation->load_segment_count].file_offset =
            program_header.offset;
        validation->segments[validation->load_segment_count].virtual_address =
            program_header.vaddr;
        validation->segments[validation->load_segment_count].file_size =
            program_header.filesz;
        validation->segments[validation->load_segment_count].memory_size =
            program_header.memsz;
        validation->segments[validation->load_segment_count].page_count =
            page_count;
        validation->segments[validation->load_segment_count].flags =
            program_header.flags;
        ++validation->load_segment_count;
        validation->total_image_pages += page_count;

        if ((header.entry >= program_header.vaddr) &&
            (header.entry < memory_end) &&
            ((program_header.flags & BORING_ELF_PF_X) != 0U) &&
            ((program_header.flags & BORING_ELF_PF_W) == 0U)) {
            entry_ok = true;
        }
    }

    return (validation->load_segment_count != 0U) && entry_ok &&
           ring3_user_range_valid((uintptr_t)header.entry, 1U);
}

static bool add_owned_page(struct boring_elf_image *image,
                           uintptr_t virtual_address,
                           uint64_t physical_address,
                           bool writable,
                           bool executable) {
    struct boring_elf_owned_page *page;

    if ((image == NULL) ||
        (image->owned_page_count >=
         (uint16_t)(BORING_ELF_MAX_IMAGE_PAGES + BORING_ELF_STACK_PAGES))) {
        return false;
    }

    page = &image->pages[image->owned_page_count];
    page->virtual_address = virtual_address;
    page->physical_address = physical_address;
    page->writable = writable;
    page->executable = executable;
    page->mapped = false;
    ++image->owned_page_count;
    return true;
}

bool boring_elf_unload(struct boring_elf_image *image) {
    bool success = true;

    if ((image == NULL) || (image->process == NULL)) {
        return false;
    }

    while (image->owned_page_count != 0U) {
        struct boring_elf_owned_page *page;

        --image->owned_page_count;
        page = &image->pages[image->owned_page_count];
        if (page->mapped &&
            !address_space_unmap_page(&image->process->address_space,
                                      page->virtual_address)) {
            success = false;
        } else {
            page->mapped = false;
        }
        if ((page->physical_address != 0ULL) &&
            !pmm_free_frame(page->physical_address)) {
            success = false;
        }
        page->virtual_address = 0U;
        page->physical_address = 0ULL;
    }

    return success;
}

static bool map_owned_page(struct boring_elf_image *image,
                           uintptr_t virtual_address,
                           bool writable,
                           bool executable,
                           uint8_t **kernel_page) {
    uint64_t physical_address;
    struct boring_elf_owned_page *owned;

    if ((image == NULL) || (image->process == NULL) ||
        (kernel_page == NULL) || !pmm_alloc_frame(&physical_address) ||
        !add_owned_page(image, virtual_address, physical_address,
                        writable, executable)) {
        if (physical_address != 0ULL) {
            (void)pmm_free_frame(physical_address);
        }
        return false;
    }

    owned = &image->pages[image->owned_page_count - 1U];
    if (!physical_page_pointer(physical_address, kernel_page)) {
        return false;
    }
    zero_page(*kernel_page);

    if (!ring3_user_map_page_permissions(&image->process->address_space,
                                         virtual_address, physical_address,
                                         writable, executable)) {
        return false;
    }
    owned->mapped = true;

    return ring3_user_mapping_permissions_valid(
        &image->process->address_space, virtual_address, physical_address,
        writable, executable);
}

bool boring_elf_load(struct process *process,
                     const uint8_t *module_bytes,
                     size_t module_size,
                     uintptr_t stack_base,
                     size_t stack_size,
                     struct boring_elf_image *image) {
    struct boring_elf_validation validation;
    uint16_t segment_index;
    struct ring3_user_mapping_info entry_info;

    if ((image == NULL) || (process == NULL) ||
        !process_is_alive(process) || process->address_space.bootstrap ||
        (process_current() == process) ||
        (stack_size != (size_t)(BORING_ELF_STACK_PAGES *
                                BORING_ELF_PAGE_SIZE)) ||
        !boring_elf_validate(module_bytes, module_size, stack_base, stack_size,
                             &validation)) {
        return false;
    }

    image_clear(image);
    image->process = process;
    image->entry = validation.entry;
    image->stack_base = stack_base;
    image->stack_top = stack_base + (uintptr_t)stack_size;
    image->module_size = (uint64_t)module_size;
    image->program_header_count = validation.program_header_count;
    image->load_segment_count = validation.load_segment_count;
    for (segment_index = 0U;
         segment_index < validation.load_segment_count; ++segment_index) {
        image->segments[segment_index] = validation.segments[segment_index];
    }

    for (segment_index = 0U;
         segment_index < validation.load_segment_count; ++segment_index) {
        const struct boring_elf_segment *segment =
            &validation.segments[segment_index];
        const bool writable =
            (segment->flags & BORING_ELF_PF_W) != 0U;
        const bool executable =
            (segment->flags & BORING_ELF_PF_X) != 0U;
        uint64_t page_index;
        uint64_t remaining_file = segment->file_size;

        for (page_index = 0ULL; page_index < segment->page_count;
             ++page_index) {
            const uint64_t page_delta = page_index * BORING_ELF_PAGE_SIZE;
            const uintptr_t virtual_address =
                (uintptr_t)(segment->virtual_address + page_delta);
            uint8_t *kernel_page = NULL;
            size_t copy_length = 0U;

            if (!map_owned_page(image, virtual_address, writable, executable,
                                &kernel_page)) {
                (void)boring_elf_unload(image);
                return false;
            }

            if (remaining_file != 0ULL) {
                copy_length = (remaining_file > BORING_ELF_PAGE_SIZE) ?
                    (size_t)BORING_ELF_PAGE_SIZE : (size_t)remaining_file;
                byte_copy(kernel_page,
                          &module_bytes[(size_t)(segment->file_offset +
                                                 page_delta)],
                          copy_length);
                remaining_file -= (uint64_t)copy_length;
            }
        }

        if (remaining_file != 0ULL) {
            (void)boring_elf_unload(image);
            return false;
        }
    }

    {
        uint8_t *stack_page = NULL;
        if (!map_owned_page(image, stack_base, true, false, &stack_page)) {
            (void)boring_elf_unload(image);
            return false;
        }
        (void)stack_page;
    }

    if (!ring3_user_query_mapping(&process->address_space, image->entry,
                                  &entry_info) ||
        !entry_info.executable || entry_info.writable ||
        !ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        (void)boring_elf_unload(image);
        return false;
    }

    return true;
}
