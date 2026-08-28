#include <boring/cpu.h>
#include <boring/elf_boot.h>
#include <boring/m37_desktop_test.h>
#include <boring/serial.h>
#include <boring/syscall_test.h>

bool syscall_test_exception_armed(void) { return false; }
void syscall_test_run(void) { m37_desktop_test_run(elf_boot_module_response()); }
void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    (void)frame;
    serial_write_string("M37 desktop FAILED: unexpected exception\n");
    x86_64_halt_forever();
}
