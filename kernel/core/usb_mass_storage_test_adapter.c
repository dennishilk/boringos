#include <boring/cpu.h>
#include <boring/serial.h>
#include <boring/syscall_test.h>
#include <boring/usb_mass_storage_test.h>

bool syscall_test_exception_armed(void) { return false; }
void syscall_test_run(void) { usb_mass_storage_test_run(); }
void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    (void)frame;
    serial_write_string("M60 USB MASS STORAGE TEST FAILED: unexpected exception\n");
    x86_64_halt_forever();
}
