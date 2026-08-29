#include <boring/cpu.h>
#include <boring/m59_physical_smoke_test.h>
#include <boring/serial.h>
#include <boring/syscall_test.h>

bool syscall_test_exception_armed(void) { return false; }

void syscall_test_run(void) { m59_physical_smoke_test_run(); }

void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    (void)frame;
    serial_write_string("M59 PHYSICAL SMOKE FAILED: unexpected exception\n");
    x86_64_halt_forever();
}
