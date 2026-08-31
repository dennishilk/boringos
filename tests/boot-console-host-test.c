#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boring/boot_console.h>
#include <boring/framebuffer.h>
#include <boring/graphics.h>
#include <boring/serial.h>

#define TEST_WIDTH 800ULL
#define TEST_HEIGHT 600ULL
#define TEST_PITCH (TEST_WIDTH * 4ULL)
#define TEST_BYTES (TEST_PITCH * TEST_HEIGHT)
#define TEST_GUARD 64U
#define SERIAL_LOG_CAPACITY 32768U

static uint8_t framebuffer_bytes[TEST_GUARD + (size_t)TEST_BYTES + TEST_GUARD];
static char serial_log[SERIAL_LOG_CAPACITY];
static size_t serial_log_length;

static void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "boot-console-host-test: FAIL: %s\n", message);
        exit(1);
    }
}

static void serial_append(const char *text) {
    size_t index = 0U;

    if (text == NULL) {
        return;
    }
    while ((text[index] != '\0') &&
           (serial_log_length + 1U < (size_t)SERIAL_LOG_CAPACITY)) {
        serial_log[serial_log_length] = text[index];
        ++serial_log_length;
        ++index;
    }
    serial_log[serial_log_length] = '\0';
}

void serial_init(void) {
}

void serial_write_bytes(const char *data, size_t length) {
    size_t index;

    if (data == NULL) {
        return;
    }
    for (index = 0U; index < length; ++index) {
        char value[2];
        value[0] = data[index];
        value[1] = '\0';
        serial_append(value);
    }
}

void serial_write_string(const char *text) {
    serial_append(text);
}

char serial_read_char_blocking(void) {
    return '\0';
}

void serial_write_u64(uint64_t value) {
    char digits[21];
    size_t count = 0U;
    char ordered[21];
    size_t index;

    do {
        digits[count] = (char)('0' + (char)(value % 10ULL));
        value /= 10ULL;
        ++count;
    } while ((value != 0ULL) && (count < sizeof(digits)));
    for (index = 0U; index < count; ++index) {
        ordered[index] = digits[count - index - 1U];
    }
    ordered[count] = '\0';
    serial_append(ordered);
}

void serial_write_hex_u64(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char output[19];
    size_t index;

    output[0] = '0';
    output[1] = 'x';
    for (index = 0U; index < 16U; ++index) {
        const uint32_t shift = (uint32_t)((15U - index) * 4U);
        output[index + 2U] = hex[(value >> shift) & 0x0fULL];
    }
    output[18] = '\0';
    serial_append(output);
}

static uint64_t framebuffer_hash(void) {
    uint64_t hash = 1469598103934665603ULL;
    size_t index;

    for (index = TEST_GUARD;
         index < TEST_GUARD + (size_t)TEST_BYTES; ++index) {
        hash ^= (uint64_t)framebuffer_bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool guards_intact(void) {
    size_t index;

    for (index = 0U; index < TEST_GUARD; ++index) {
        if ((framebuffer_bytes[index] != 0xa5U) ||
            (framebuffer_bytes[TEST_GUARD + (size_t)TEST_BYTES + index] !=
             0xa5U)) {
            return false;
        }
    }
    return true;
}

static size_t packed_color_count(uint32_t packed) {
    const uint8_t first = (uint8_t)(packed & 0xffU);
    const uint8_t second = (uint8_t)((packed >> 8U) & 0xffU);
    const uint8_t third = (uint8_t)((packed >> 16U) & 0xffU);
    const uint8_t fourth = (uint8_t)((packed >> 24U) & 0xffU);
    size_t count = 0U;
    size_t offset;

    for (offset = TEST_GUARD;
         offset < TEST_GUARD + (size_t)TEST_BYTES; offset += 4U) {
        if ((framebuffer_bytes[offset] == first) &&
            (framebuffer_bytes[offset + 1U] == second) &&
            (framebuffer_bytes[offset + 2U] == third) &&
            (framebuffer_bytes[offset + 3U] == fourth)) {
            ++count;
        }
    }
    return count;
}

static void verify_initial_history(void) {
    struct boring_boot_console_stage_info event;

    check(boring_boot_console_get_history(0U, &event) &&
          (event.stage == BORING_BOOT_STAGE_CPU_INVENTORY) &&
          (event.status == BORING_BOOT_STATUS_PENDING),
          "early history event 0");
    check(boring_boot_console_get_history(1U, &event) &&
          (event.stage == BORING_BOOT_STAGE_CPU_INVENTORY) &&
          (event.status == BORING_BOOT_STATUS_OK),
          "early history event 1");
    check(boring_boot_console_get_history(2U, &event) &&
          (event.stage == BORING_BOOT_STAGE_PCI_INVENTORY) &&
          (event.status == BORING_BOOT_STATUS_PENDING),
          "early history event 2");
    check(boring_boot_console_get_history(3U, &event) &&
          (event.stage == BORING_BOOT_STAGE_SMBIOS) &&
          (event.status == BORING_BOOT_STATUS_FAIL) &&
          (strcmp(event.reason, "table checksum") == 0),
          "early history FAIL reason");
}

static void verify_bounded_history(void) {
    struct boring_boot_console_stats stats;
    struct boring_boot_console_stage_info event;
    size_t index;

    boring_boot_console_test_reset();
    for (index = 0U; index < 67U; ++index) {
        const enum boring_boot_console_stage stage =
            (enum boring_boot_console_stage)(index %
                (size_t)BORING_BOOT_STAGE_COUNT);
        const bool recorded = ((index & 1U) == 0U) ?
            boring_boot_console_pending(stage) :
            boring_boot_console_ok(stage);
        check(recorded, "bounded history record");
    }
    check(boring_boot_console_get_stats(&stats), "bounded history stats");
    check(stats.history_count ==
          (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY,
          "bounded history capacity");
    check(stats.history_dropped == 3ULL, "bounded history drop count");
    check(stats.pre_activation_framebuffer_writes == 0ULL,
          "bounded history remains framebuffer-free");
    check(boring_boot_console_get_history(0U, &event) &&
          (event.stage == BORING_BOOT_STAGE_PMM) &&
          (event.status == BORING_BOOT_STATUS_OK),
          "bounded history retained order first");
    check(boring_boot_console_get_history(
              (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY - 1U, &event) &&
          (event.stage == BORING_BOOT_STAGE_PMM) &&
          (event.status == BORING_BOOT_STATUS_PENDING),
          "bounded history retained order last");
}

int main(void) {
    struct boring_framebuffer surface;
    struct boring_boot_console_stats stats;
    struct boring_boot_console_stage_info stage;
    uint64_t before;
    uint64_t after_handoff;
    uint32_t success;
    uint32_t pending;
    uint32_t failure;

    memset(framebuffer_bytes, 0xa5, sizeof(framebuffer_bytes));
    check(boring_framebuffer_surface_init(
              &surface, &framebuffer_bytes[TEST_GUARD],
              TEST_WIDTH, TEST_HEIGHT, TEST_PITCH, 32U,
              BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
              8U, 16U, 8U, 8U, 8U, 0U),
          "test framebuffer surface");
    boring_boot_console_test_reset();

    before = framebuffer_hash();
    check(boring_boot_console_pending(BORING_BOOT_STAGE_CPU_INVENTORY),
          "CPU pending");
    check(boring_boot_console_ok(BORING_BOOT_STAGE_CPU_INVENTORY),
          "CPU OK");
    check(boring_boot_console_pending(BORING_BOOT_STAGE_PCI_INVENTORY),
          "PCI pending");
    check(boring_boot_console_fail(BORING_BOOT_STAGE_SMBIOS,
                                   "table checksum"),
          "SMBIOS FAIL");
    check(framebuffer_hash() == before,
          "no framebuffer write before activation");
    check(guards_intact(), "pre-activation framebuffer guards");
    check(boring_boot_console_get_stats(&stats), "pre-activation stats");
    check((stats.history_count == 4U) &&
          (stats.render_count == 0ULL) &&
          (stats.pre_activation_framebuffer_writes == 0ULL) &&
          !stats.framebuffer_activated && !stats.framebuffer_active,
          "static early history has no framebuffer dependency");
    verify_initial_history();

    check(boring_boot_console_activate(&surface), "framebuffer activation");
    check(guards_intact(), "activation framebuffer bounds");
    check(framebuffer_hash() != before, "activation rendered pixels");
    check(boring_boot_console_get_stats(&stats), "activation stats");
    check(stats.framebuffer_activated && stats.framebuffer_active &&
          (stats.render_count == 1ULL) &&
          (stats.pre_activation_framebuffer_writes == 0ULL),
          "safe activation state");

    check(boring_boot_console_get_stage(BORING_BOOT_STAGE_CPU_INVENTORY,
                                        &stage) &&
          (stage.status == BORING_BOOT_STATUS_OK) &&
          (strcmp(stage.label, "CPU inventory") == 0),
          "OK stage replay");
    check(boring_boot_console_get_stage(BORING_BOOT_STAGE_PCI_INVENTORY,
                                        &stage) &&
          (stage.status == BORING_BOOT_STATUS_PENDING),
          "pending stage replay");
    check(boring_boot_console_get_stage(BORING_BOOT_STAGE_SMBIOS, &stage) &&
          (stage.status == BORING_BOOT_STATUS_FAIL) &&
          (strcmp(stage.reason, "table checksum") == 0),
          "FAIL stage replay with reason");

    success = boring_color_pack(&surface, 0x72U, 0xd6U, 0x8aU);
    pending = boring_color_pack(&surface, 0xe4U, 0xb8U, 0x68U);
    failure = boring_color_pack(&surface, 0xffU, 0x60U, 0x60U);
    check(packed_color_count(success) != 0U, "OK rendered green");
    check(packed_color_count(pending) != 0U, "pending rendered amber");
    check(packed_color_count(failure) != 0U, "FAIL rendered red");
    check(strstr(serial_log, "BOOT-CONSOLE [ OK ] CPU inventory\n") != NULL,
          "serial OK mirror");
    check(strstr(serial_log,
                 "BOOT-CONSOLE [FAIL] SMBIOS: table checksum\n") != NULL,
          "serial FAIL reason mirror");
    check(strstr(serial_log,
                 "BOOT-CONSOLE framebuffer activated at safe normal present\n") !=
          NULL,
          "safe activation serial witness");

    check(boring_boot_console_pending(BORING_BOOT_STAGE_DESKTOP_PRESENT),
          "desktop pending live update");
    check(guards_intact(), "live update framebuffer bounds");
    before = framebuffer_hash();
    boring_boot_console_desktop_handoff();
    after_handoff = framebuffer_hash();
    check(after_handoff == before,
          "desktop handoff performs no framebuffer redraw");
    check(boring_boot_console_ok(BORING_BOOT_STAGE_CPU_INVENTORY),
          "post-handoff serial/state update");
    check(framebuffer_hash() == after_handoff,
          "post-handoff updates do not repaint desktop");
    check(boring_boot_console_get_stats(&stats), "handoff stats");
    check(stats.desktop_handoff_complete && !stats.framebuffer_active,
          "final desktop handoff state");
    check(guards_intact(), "handoff framebuffer bounds");

    before = framebuffer_hash();
    verify_bounded_history();
    check(framebuffer_hash() == before,
          "bounded early history remains framebuffer-free after reset");

    puts("boot-console-host-test: bounded static history: PASS");
    puts("boot-console-host-test: no heap or pre-activation framebuffer: PASS");
    puts("boot-console-host-test: ordered replay + OK/pending/FAIL: PASS");
    puts("boot-console-host-test: framebuffer bounds + desktop handoff: PASS");
    puts("boot-console-host-test: serial mirror fail-open contract: PASS");
    return 0;
}
