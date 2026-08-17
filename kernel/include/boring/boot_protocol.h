#ifndef BORING_BOOT_PROTOCOL_H
#define BORING_BOOT_PROTOCOL_H

#include <stdint.h>

/*
 * Limine boot protocol constants and memory-map structures required by the
 * current BoringKernel milestones.
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
#define BORING_LIMINE_MEMMAP_REQUEST_ID \
    { BORING_LIMINE_COMMON_MAGIC, \
      0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL }

#define BORING_LIMINE_MEMMAP_USABLE                 0ULL
#define BORING_LIMINE_MEMMAP_RESERVED               1ULL
#define BORING_LIMINE_MEMMAP_ACPI_RECLAIMABLE       2ULL
#define BORING_LIMINE_MEMMAP_ACPI_NVS               3ULL
#define BORING_LIMINE_MEMMAP_BAD_MEMORY             4ULL
#define BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5ULL
#define BORING_LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6ULL
#define BORING_LIMINE_MEMMAP_FRAMEBUFFER            7ULL
#define BORING_LIMINE_MEMMAP_RESERVED_MAPPED        8ULL

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

#endif
