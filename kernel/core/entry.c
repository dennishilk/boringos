#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu.h>
#include <boring/kernel.h>
#include <boring/serial.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = BORING_LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = BORING_LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = BORING_LIMINE_REQUESTS_END_MARKER;

void boring_kernel_entry(void) {
    if (!BORING_LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        x86_64_halt_forever();
    }

    serial_init();
    serial_write_string("BoringOS booting...\n");
    serial_write_string("BoringKernel 0.0.1-dev\n");
    serial_write_string("Arch: x86_64\n");
    serial_write_string("Hello from BoringKernel.\n");

    x86_64_halt_forever();
}
