#include <boring/ahci_persistence_test.h>
#include <boring/cpu.h>
#include <boring/serial.h>
#include <boring/syscall_test.h>

bool syscall_test_exception_armed(void) { return false; }
void syscall_test_run(void) {
    ahci_persistence_test_run();
    x86_64_halt_forever();
}
void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    (void)frame;
    serial_write_string("M57 AHCI PERSISTENCE FAILED: unexpected exception\n");
    x86_64_halt_forever();
}
