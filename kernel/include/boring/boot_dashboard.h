#ifndef BORING_BOOT_DASHBOARD_H
#define BORING_BOOT_DASHBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/framebuffer.h>

struct boring_boot_dashboard_info {
    const char *kernel_name;
    const char *kernel_version;
    const char *arch;
    uint64_t memory_bytes;
    const char *root_fs;
    const char *block_device;
    bool pmm_online;
    bool vmm_online;
    bool irq_online;
    bool ring3_available;
    bool vfs_online;
    bool boringfs_online;
};

bool boring_boot_dashboard_render(
    const struct boring_framebuffer *surface,
    const struct boring_boot_dashboard_info *info);

#endif
