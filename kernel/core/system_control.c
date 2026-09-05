#include <stdbool.h>
#include <stdint.h>

#include <boring/acpi.h>
#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/platform_reset.h>
#include <boring/serial.h>
#include <boring/syscall_abi.h>
#include <boring/system_control.h>

enum system_power_state {
    SYSTEM_POWER_RUNNING = 0,
    SYSTEM_POWER_TRANSITION_IN_PROGRESS = 1
};

static volatile enum system_power_state power_state = SYSTEM_POWER_RUNNING;

static void restore_interrupts(bool enabled) {
    if (enabled) {
        x86_64_interrupts_enable();
    }
}

bool system_control_spawn_allowed(void) {
    return power_state == SYSTEM_POWER_RUNNING;
}

bool system_control_begin(uint32_t action) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    if ((action != BORING_SYSTEM_REBOOT) &&
        (action != BORING_SYSTEM_POWEROFF)) {
        return false;
    }

    x86_64_interrupts_disable();
    if (power_state != SYSTEM_POWER_RUNNING) {
        restore_interrupts(interrupts_were_enabled);
        return false;
    }
    power_state = SYSTEM_POWER_TRANSITION_IN_PROGRESS;
    restore_interrupts(interrupts_were_enabled);

    serial_write_string((action == BORING_SYSTEM_REBOOT) ?
                        "M63_REBOOT_REQUESTED\n" :
                        "M63_SHUTDOWN_REQUESTED\n");
    serial_write_string("M63_NEW_SPAWN_GATE_ACTIVE\n");
    return true;
}

static void transition_failure(const char *witness) __attribute__((noreturn));
static void transition_failure(const char *witness) {
    serial_write_string(witness);
    x86_64_halt_forever();
}

void system_control_execute(uint32_t action) {
    struct boring_acpi_stats acpi;

    serial_write_string("M63_STORAGE_SYNC_BEGIN\n");
    if (block_device_flush_all() != BLOCK_DEVICE_RESULT_OK) {
        transition_failure("M63_STORAGE_SYNC_FAILED\n");
    }
    serial_write_string("M63_STORAGE_SYNC_OK\n");

    if (!boring_acpi_get_stats(&acpi) || !acpi.initialized ||
        !acpi.fadt_found) {
        if (action == BORING_SYSTEM_REBOOT) {
            serial_write_string("M63_REBOOT_HW_ENTER\n");
            x86_64_platform_reset_fallback();
        }
        transition_failure("M63_POWEROFF_ACPI_UNAVAILABLE\n");
    }
    serial_write_string("M63_ACPI_TABLES_OK\n");

    if (action == BORING_SYSTEM_REBOOT) {
        serial_write_string("M63_REBOOT_HW_ENTER\n");
        if (!boring_acpi_reset()) {
            x86_64_platform_reset_fallback();
        }
        transition_failure("M63_REBOOT_HW_FAILED\n");
    }

    if ((action != BORING_SYSTEM_POWEROFF) || !acpi.s5_supported ||
        acpi.hardware_reduced) {
        transition_failure("M63_POWEROFF_ACPI_S5_UNAVAILABLE\n");
    }
    serial_write_string("M63_POWEROFF_HW_ENTER\n");
    if (!boring_acpi_poweroff()) {
        transition_failure("M63_POWEROFF_HW_FAILED\n");
    }
    transition_failure("M63_POWEROFF_HW_RETURNED\n");
}
