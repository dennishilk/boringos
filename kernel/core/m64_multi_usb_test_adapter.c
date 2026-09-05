#include <boring/cpu.h>
#include <boring/m64_multi_usb_test.h>
#include <boring/serial.h>
#include <boring/syscall_test.h>

bool syscall_test_exception_armed(void) { return false; }

void syscall_test_run(void) { m64_multi_usb_test_run(); }

void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    (void)frame;
    serial_write_string("M64 multi-USB FAILED: unexpected exception\n");
    x86_64_halt_forever();
}
