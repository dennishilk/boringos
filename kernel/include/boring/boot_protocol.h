#ifndef BORING_BOOT_PROTOCOL_H
#define BORING_BOOT_PROTOCOL_H

#include <stdint.h>

/*
 * Limine boot protocol constants required by this milestone only.
 * Provenance: Limine protocol request/base-revision definitions at
 * commit 80ef54bed402b8c0b672a707c1df4c532f3428ad (0BSD).
 */
#define BORING_LIMINE_REQUESTS_START_MARKER \
    { 0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL, \
      0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL }
#define BORING_LIMINE_REQUESTS_END_MARKER \
    { 0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL }
#define BORING_LIMINE_BASE_REVISION(N) \
    { 0xf9562b2d5c95a6c8ULL, 0x6a7b384944536bdcULL, (N) }
#define BORING_LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0ULL)

#endif
