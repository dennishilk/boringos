#include <boring/console_test.h>
#include <boring/elf_boot.h>
#include <boring/exception.h>
#include <boring/syscall_test.h>

bool syscall_test_exception_armed(void) {
    return console_test_exception_armed();
}

void syscall_test_run(void) {
    console_test_run(elf_boot_module_response());
}

void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    console_test_handle_exception(frame);
}
