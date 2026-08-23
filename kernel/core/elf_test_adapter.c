#include <boring/elf_boot.h>
#include <boring/elf_test.h>
#include <boring/exception.h>
#include <boring/syscall_test.h>

/*
 * Development/final acceptance adapter for TEST_MODE=elf. The Makefile maps
 * only that isolated build mode onto the existing special-test dispatch slot
 * in entry.c while replacing syscall_test.c with this adapter. The ordinary
 * TEST_MODE=syscall build continues to compile and run the original syscall
 * acceptance unchanged.
 */
bool syscall_test_exception_armed(void) {
    return elf_test_exception_armed();
}

void syscall_test_run(void) {
    elf_test_run(elf_boot_module_response());
}

void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    elf_test_handle_exception(frame);
}
