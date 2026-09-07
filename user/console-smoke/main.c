#include <stddef.h>
#include <stdint.h>

#include <boring/console_smoke.h>
#include <boring/runtime.h>
#include <boring/syscall.h>

static const char console_message[] = "console write from BoringOS userspace\n";

volatile struct boring_console_smoke_result boring_console_smoke_result
    __attribute__((section(".runtime_result"), aligned(16))) = { 0ULL };

volatile uint64_t boring_console_data_marker =
    BORING_CONSOLE_SMOKE_DATA_MARKER;

volatile uint64_t boring_console_bss_probe
    __attribute__((section(".bss.runtime_probe")));

static uint16_t console_read_cs(void) {
    uint16_t selector;

    __asm__ volatile("movw %%cs, %0" : "=r"(selector));
    return selector;
}

int boring_main(void) {
    uint8_t input = 0U;
    uint8_t echo[BORING_CONSOLE_SMOKE_ECHO_LENGTH];
    uint64_t pid;
    long write_result;
    long read_result;
    long echo_result;

    boring_console_smoke_result.cs = (uint64_t)console_read_cs();
    boring_console_smoke_result.entered = 1ULL;

    if (boring_console_data_marker != BORING_CONSOLE_SMOKE_DATA_MARKER) {
        return 201;
    }
    boring_console_smoke_result.data_ok = 1ULL;

    if (boring_console_bss_probe != 0ULL) {
        return 202;
    }
    boring_console_smoke_result.bss_zero = 1ULL;
    boring_console_bss_probe = BORING_CONSOLE_SMOKE_BSS_MARKER;

    pid = boring_getpid();
    boring_console_smoke_result.getpid_result = pid;
    if (pid != 1ULL) {
        return 203;
    }
    boring_console_smoke_result.getpid_ok = 1ULL;

    write_result = boring_console_write(
        console_message, (size_t)BORING_CONSOLE_SMOKE_MESSAGE_LENGTH);
    boring_console_smoke_result.write_result = (uint64_t)write_result;
    if (write_result != (long)BORING_CONSOLE_SMOKE_MESSAGE_LENGTH) {
        return 204;
    }
    boring_console_smoke_result.write_ok = 1ULL;

    read_result = boring_console_read(&input, 1U);
    boring_console_smoke_result.read_result = (uint64_t)read_result;
    boring_console_smoke_result.read_value = (uint64_t)input;
    if ((read_result != 1L) ||
        (input != (uint8_t)BORING_CONSOLE_SMOKE_INPUT_BYTE)) {
        return 205;
    }
    boring_console_smoke_result.read_ok = 1ULL;

    echo[0] = input;
    echo[1] = (uint8_t)'\n';
    echo_result = boring_console_write(
        &echo[0], (size_t)BORING_CONSOLE_SMOKE_ECHO_LENGTH);
    boring_console_smoke_result.echo_result = (uint64_t)echo_result;
    if (echo_result != (long)BORING_CONSOLE_SMOKE_ECHO_LENGTH) {
        return 206;
    }
    boring_console_smoke_result.echo_ok = 1ULL;
    boring_console_smoke_result.sysret_resume = 1ULL;
    boring_console_smoke_result.ready_to_return = 1ULL;

    return BORING_CONSOLE_SMOKE_MAIN_RETURN;
}
