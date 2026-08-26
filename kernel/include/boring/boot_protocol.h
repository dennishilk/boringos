#ifndef BORING_BOOT_PROTOCOL_H
#define BORING_BOOT_PROTOCOL_H

#include <stdint.h>

/*
 * Limine boot protocol constants and structures required by the current
 * BoringKernel milestones.
 *
 * Provenance: Limine Protocol header, commit
 * 4e1587972c148d43b2f397e4e5983bdd6c2a55a0 (0BSD).
 * Only protocol declarations/constants are reproduced here; no Limine
 * implementation code is part of BoringKernel.
 */
#define BORING_LIMINE_REQUESTS_START_MARKER \
    { 0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL, \
      0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL }
#define BORING_LIMINE_REQUESTS_END_MARKER \
    { 0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL }
#define BORING_LIMINE_BASE_REVISION(N) \
    { 0xf9562b2d5c95a6c8ULL, 0x6a7b384944536bdcULL, (N) }
#define BORING_LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0ULL)

#define BORING_LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL

#define BORING_LIMINE_HHDM_REQUEST_ID \
    { BORING_LIMINE_COMMON_MAGIC, \
      0x48dcf1cb8ad2b852ULL, 0x63984e959a98244bULL }
#define BORING_LIMINE_PAGING_MODE_REQUEST_ID \
    { BORING_LIMINE_COMMON_MAGIC, \
      0x95c1a0edab0944cbULL, 0xa4e5cb3842f7488aULL }
#define BORING_LIMINE_MEMMAP_REQUEST_ID \
    { BORING_LIMINE_COMMON_MAGIC, \
      0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL }
#define BORING_LIMINE_MODULE_REQUEST_ID \
    { BORING_LIMINE_COMMON_MAGIC, \
      0x3e7e279702be32afULL, 0xca1c4f3bd1280ceeULL }
#define BORING_LIMINE_FRAMEBUFFER_REQUEST_ID \
    { BORING_LIMINE_COMMON_MAGIC, \
      0x9d5827dcd881dd75ULL, 0xa3148604f6fab11bULL }

#define BORING_LIMINE_FRAMEBUFFER_RGB 1U

#define BORING_LIMINE_PAGING_MODE_X86_64_4LVL 0ULL
#define BORING_LIMINE_PAGING_MODE_X86_64_5LVL 1ULL

#define BORING_LIMINE_MEMMAP_USABLE                 0ULL
#define BORING_LIMINE_MEMMAP_RESERVED               1ULL
#define BORING_LIMINE_MEMMAP_ACPI_RECLAIMABLE       2ULL
#define BORING_LIMINE_MEMMAP_ACPI_NVS               3ULL
#define BORING_LIMINE_MEMMAP_BAD_MEMORY             4ULL
#define BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5ULL
#define BORING_LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6ULL
#define BORING_LIMINE_MEMMAP_FRAMEBUFFER            7ULL
#define BORING_LIMINE_MEMMAP_RESERVED_MAPPED        8ULL

struct boring_limine_uuid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};

struct boring_limine_file {
    uint64_t revision;
    void *address;
    uint64_t size;
    char *path;
    char *string;
    uint32_t media_type;
    uint32_t unused;
    uint8_t tftp_ipv4[4];
    uint32_t tftp_port;
    uint32_t partition_index;
    uint32_t mbr_disk_id;
    struct boring_limine_uuid gpt_disk_uuid;
    struct boring_limine_uuid gpt_part_uuid;
    struct boring_limine_uuid part_uuid;
};

struct boring_limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct boring_limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct boring_limine_hhdm_response *response;
};

struct boring_limine_paging_mode_response {
    uint64_t revision;
    uint64_t mode;
};

struct boring_limine_paging_mode_request {
    uint64_t id[4];
    uint64_t revision;
    struct boring_limine_paging_mode_response *response;
    uint64_t mode;
    uint64_t max_mode;
    uint64_t min_mode;
};

struct boring_limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct boring_limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct boring_limine_memmap_entry **entries;
};

struct boring_limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct boring_limine_memmap_response *response;
};

struct boring_limine_video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct boring_limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
    uint64_t mode_count;
    struct boring_limine_video_mode **modes;
};

struct boring_limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct boring_limine_framebuffer **framebuffers;
};

struct boring_limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct boring_limine_framebuffer_response *response;
};

struct boring_limine_internal_module {
    const char *path;
    const char *string;
    uint64_t flags;
};

struct boring_limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    struct boring_limine_file **modules;
};

struct boring_limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    struct boring_limine_module_response *response;
    uint64_t internal_module_count;
    struct boring_limine_internal_module **internal_modules;
};

#endif
