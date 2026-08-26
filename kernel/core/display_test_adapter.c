#include <boring/cpu.h>
#include <boring/display_test.h>
#include <boring/elf_boot.h>
#include <boring/serial.h>
#include <boring/syscall_test.h>

/*
 * M34 uses the same narrow special-test entry seam as the established ELF
 * and runtime acceptances. The real work remains display_test_run(), which
 * creates three distinct Ring-3 processes/address spaces from Limine modules.
 */
bool syscall_test_exception_armed(void) {
    return false;
}

void syscall_test_run(void) {
    display_test_run(elf_boot_module_response());
}

void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    (void)frame;
    serial_write_string("M34 display acceptance unexpected exception\n");
    x86_64_halt_forever();
}
