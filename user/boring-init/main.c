#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/runtime.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

static volatile uint64_t init_state;

static void init_idle_forever(void) __attribute__((noreturn));
static void init_idle_forever(void) {
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static bool init_write_exact(const char *message, size_t length) {
    const long result = boring_console_write(message, length);

    return result == (long)length;
}

#ifdef BORING_INIT_LAUNCH_SHELL
static bool init_launch_safety(void) {
    static const char shell_name[] = "boring-shell";
    static const char unknown_name[] = "not-shell";
    static const char embedded_nul[3] = { 'x', '\0', 'y' };
    const char *const kernel_pointer =
        (const char *)(uintptr_t)0xffff800000000000ULL;
    const char *const overflow_pointer =
        (const char *)(uintptr_t)(UINTPTR_MAX - 1U);

    return (boring_launch(NULL, 1U) == -(long)BORING_SYSCALL_EFAULT) &&
           (boring_launch(kernel_pointer, 1U) ==
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_launch(overflow_pointer, 4U) ==
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_launch(shell_name, 0U) ==
            -(long)BORING_SYSCALL_EINVAL) &&
           (boring_launch(shell_name,
                          (size_t)BORING_SYSCALL_LAUNCH_NAME_MAX + 1U) ==
            -(long)BORING_SYSCALL_ENAMETOOLONG) &&
           (boring_launch(embedded_nul, sizeof(embedded_nul)) ==
            -(long)BORING_SYSCALL_EINVAL) &&
           (boring_launch(unknown_name, sizeof(unknown_name) - 1U) ==
            -(long)BORING_SYSCALL_ENOENT);
}
#endif

int boring_main(void) {
    static const char starting[] = "boring-init: starting\n";
    static const char pid_ok[] = "boring-init: pid 1\n";
    static const char online[] = "boring-init: online\n";
    static const char failed[] = "boring-init: FAILED\n";
    const uint64_t pid = boring_getpid();

    if (!init_write_exact(starting, sizeof(starting) - 1U) ||
        (pid != 1ULL) ||
        !init_write_exact(pid_ok, sizeof(pid_ok) - 1U)) {
        (void)init_write_exact(failed, sizeof(failed) - 1U);
        init_idle_forever();
    }

    init_state = 1ULL;
    if ((init_state != 1ULL) ||
        !init_write_exact(online, sizeof(online) - 1U)) {
        (void)init_write_exact(failed, sizeof(failed) - 1U);
        init_idle_forever();
    }

#ifdef BORING_INIT_LAUNCH_SHELL
    {
        static const char launch_message[] =
            "boring-init: launching boring-shell\n";
        static const char shell_name[] = "boring-shell";

        if (!init_launch_safety() ||
            !init_write_exact(launch_message, sizeof(launch_message) - 1U)) {
            (void)init_write_exact(failed, sizeof(failed) - 1U);
            init_idle_forever();
        }

        (void)boring_launch(shell_name, sizeof(shell_name) - 1U);
        (void)init_write_exact(failed, sizeof(failed) - 1U);
        init_idle_forever();
    }
#else
    init_idle_forever();
#endif
}
