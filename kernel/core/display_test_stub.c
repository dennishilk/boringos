#include <stdbool.h>

#include <boring/display_test.h>

bool __attribute__((weak)) boring_display_test_process_exit_prepare(
    struct process *process) {
    (void)process;
    return false;
}
