#ifndef BORING_ELF_LOADER_H
#define BORING_ELF_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct process;

#define BORING_ELF_MODULE_MAX_SIZE 65536U
#define BORING_ELF_MAX_PROGRAM_HEADERS 8U
#define BORING_ELF_MAX_LOAD_SEGMENTS 4U
#define BORING_ELF_MAX_IMAGE_PAGES 16U
#define BORING_ELF_STACK_PAGES 1U
#define BORING_ELF_PAGE_SIZE 4096ULL

#define BORING_ELF_PF_X 0x1U
#define BORING_ELF_PF_W 0x2U
#define BORING_ELF_PF_R 0x4U

struct boring_elf_segment {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t page_count;
    uint32_t flags;
};

struct boring_elf_validation {
    uintptr_t entry;
    uint16_t program_header_count;
    uint16_t load_segment_count;
    uint64_t total_image_pages;
    struct boring_elf_segment segments[BORING_ELF_MAX_LOAD_SEGMENTS];
};

struct boring_elf_owned_page {
    uintptr_t virtual_address;
    uint64_t physical_address;
    bool writable;
    bool executable;
    bool mapped;
};

struct boring_elf_image {
    struct process *process;
    uintptr_t entry;
    uintptr_t stack_base;
    uintptr_t stack_top;
    uint64_t module_size;
    uint16_t program_header_count;
    uint16_t load_segment_count;
    uint16_t owned_page_count;
    struct boring_elf_segment segments[BORING_ELF_MAX_LOAD_SEGMENTS];
    struct boring_elf_owned_page pages[BORING_ELF_MAX_IMAGE_PAGES +
                                       BORING_ELF_STACK_PAGES];
};

bool boring_elf_validate(const uint8_t *module_bytes,
                         size_t module_size,
                         uintptr_t stack_base,
                         size_t stack_size,
                         struct boring_elf_validation *validation);
bool boring_elf_load(struct process *process,
                     const uint8_t *module_bytes,
                     size_t module_size,
                     uintptr_t stack_base,
                     size_t stack_size,
                     struct boring_elf_image *image);
bool boring_elf_unload(struct boring_elf_image *image);

#endif
