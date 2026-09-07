#include <stdint.h>

#include <boring/cpu.h>
#include <boring/io.h>
#include <boring/platform_reset.h>
#include <boring/serial.h>

#define X86_RESET_CF9_PORT 0x0cf9U
#define X86_RESET_CF9_REQUEST 0x02U
#define X86_RESET_CF9_HARD 0x06U
#define X86_I8042_STATUS_PORT 0x0064U
#define X86_I8042_RESET_COMMAND 0xfeU
#define X86_I8042_INPUT_FULL 0x02U
#define X86_RESET_SPIN_LIMIT 1000000U

void x86_64_platform_reset_fallback(void) {
    uint32_t spin;

    x86_64_out8(X86_RESET_CF9_PORT, X86_RESET_CF9_REQUEST);
    x86_64_memory_barrier();
    x86_64_out8(X86_RESET_CF9_PORT, X86_RESET_CF9_HARD);
    for (spin = 0U; spin < X86_RESET_SPIN_LIMIT; ++spin) {
        x86_64_pause();
    }

    for (spin = 0U; spin < X86_RESET_SPIN_LIMIT; ++spin) {
        if ((x86_64_in8(X86_I8042_STATUS_PORT) &
             X86_I8042_INPUT_FULL) == 0U) {
            x86_64_out8(X86_I8042_STATUS_PORT, X86_I8042_RESET_COMMAND);
            break;
        }
        x86_64_pause();
    }
    for (spin = 0U; spin < X86_RESET_SPIN_LIMIT; ++spin) {
        x86_64_pause();
    }
    serial_write_string("M63_REBOOT_HW_FAILED\n");
    x86_64_halt_forever();
}
