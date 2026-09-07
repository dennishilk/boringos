#ifndef BORING_BOOT_CONSOLE_H
#define BORING_BOOT_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/framebuffer.h>

#define BORING_BOOT_CONSOLE_HISTORY_CAPACITY 64U
#define BORING_BOOT_CONSOLE_REASON_CAPACITY 64U

enum boring_boot_console_stage {
    BORING_BOOT_STAGE_CPU_INVENTORY = 0,
    BORING_BOOT_STAGE_PCI_INVENTORY,
    BORING_BOOT_STAGE_SMBIOS,
    BORING_BOOT_STAGE_PMM,
    BORING_BOOT_STAGE_VMM,
    BORING_BOOT_STAGE_HEAP,
    BORING_BOOT_STAGE_EXCEPTIONS,
    BORING_BOOT_STAGE_INPUT,
    BORING_BOOT_STAGE_IRQ,
    BORING_BOOT_STAGE_PIT,
    BORING_BOOT_STAGE_XHCI,
    BORING_BOOT_STAGE_USB_ADDRESSING,
    BORING_BOOT_STAGE_USB_DESCRIPTORS,
    BORING_BOOT_STAGE_USB_HID,
    BORING_BOOT_STAGE_USB_MASS_STORAGE,
    BORING_BOOT_STAGE_BORINGFS_ROOT,
    BORING_BOOT_STAGE_BORING_INIT,
    BORING_BOOT_STAGE_BORING_DISPLAY,
    BORING_BOOT_STAGE_BORING_WM,
    BORING_BOOT_STAGE_DESKTOP_PRESENT,
    BORING_BOOT_STAGE_COUNT
};

enum boring_boot_console_status {
    BORING_BOOT_STATUS_PENDING = 0,
    BORING_BOOT_STATUS_OK,
    BORING_BOOT_STATUS_FAIL
};

struct boring_boot_console_stage_info {
    enum boring_boot_console_stage stage;
    enum boring_boot_console_status status;
    const char *label;
    const char *reason;
};

struct boring_boot_console_stats {
    size_t history_count;
    uint64_t history_dropped;
    uint64_t render_count;
    uint64_t pre_activation_framebuffer_writes;
    bool framebuffer_activated;
    bool framebuffer_active;
    bool desktop_handoff_complete;
};

bool boring_boot_console_pending(enum boring_boot_console_stage stage);
bool boring_boot_console_ok(enum boring_boot_console_stage stage);
bool boring_boot_console_fail(enum boring_boot_console_stage stage,
                              const char *reason);

bool boring_boot_console_activate(
    const struct boring_framebuffer *surface);
bool boring_boot_console_refresh(void);
void boring_boot_console_desktop_handoff(void);

bool boring_boot_console_get_stage(
    enum boring_boot_console_stage stage,
    struct boring_boot_console_stage_info *info);
bool boring_boot_console_get_history(
    size_t index,
    struct boring_boot_console_stage_info *info);
bool boring_boot_console_get_stats(struct boring_boot_console_stats *stats);

#ifdef BORING_BOOT_CONSOLE_TEST
void boring_boot_console_test_reset(void);
#endif

#endif
