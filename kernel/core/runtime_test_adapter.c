#include <boring/elf_boot.h>
#include <boring/exception.h>
#include <boring/runtime_test.h>
#include <boring/syscall_test.h>

/*
 * TEST_MODE=runtime owns its own numeric entry-mode value. The existing
 * special-test exception hook remains the narrow dispatch seam, so the
 * ordinary syscall and Milestone-11 ELF acceptance builds keep their
 * established implementations unchanged.
 */
bool syscall_test_exception_armed(void) {
    return runtime_test_exception_armed();
}

void syscall_test_run(void) {
    runtime_test_run(elf_boot_module_response());
}

void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    runtime_test_handle_exception(frame);
}
