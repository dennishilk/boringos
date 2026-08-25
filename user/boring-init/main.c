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
static bool init_write_u64(uint64_t value) {
    char digits[21];
    size_t count = 0U;
    size_t index;

    do {
        digits[count] = (char)('0' + (char)(value % 10ULL));
        value /= 10ULL;
        ++count;
    } while (value != 0ULL);
    for (index = 0U; index < count / 2U; ++index) {
        const char temporary = digits[index];
        digits[index] = digits[count - index - 1U];
        digits[count - index - 1U] = temporary;
    }
    return init_write_exact(digits, count);
}

static bool init_write_status(int status) {
    int64_t wide = (int64_t)status;

    if (wide < 0) {
        if (!init_write_exact("-", 1U)) {
            return false;
        }
        wide = -wide;
    }
    return init_write_u64((uint64_t)wide);
}

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
        static const char exited_prefix[] =
            "boring-init: shell exited with status ";
        static const char respawn_suffix[] = "; respawning\n";
        static const char shell_name[] = "boring-shell";

        if (!init_launch_safety()) {
            (void)init_write_exact(failed, sizeof(failed) - 1U);
            init_idle_forever();
        }

        for (;;) {
            long child_pid;
            int status = 0;
            long waited;

            if (!init_write_exact(launch_message, sizeof(launch_message) - 1U)) {
                (void)init_write_exact(failed, sizeof(failed) - 1U);
                init_idle_forever();
            }

            child_pid = boring_launch(shell_name, sizeof(shell_name) - 1U);
            if (child_pid <= 0L) {
                (void)init_write_exact(failed, sizeof(failed) - 1U);
                init_idle_forever();
            }

            waited = boring_waitpid((uint64_t)child_pid, &status);
            if (waited != child_pid) {
                (void)init_write_exact(failed, sizeof(failed) - 1U);
                init_idle_forever();
            }

            if (!init_write_exact(exited_prefix,
                                  sizeof(exited_prefix) - 1U) ||
                !init_write_status(status) ||
                !init_write_exact(respawn_suffix,
                                  sizeof(respawn_suffix) - 1U)) {
                (void)init_write_exact(failed, sizeof(failed) - 1U);
                init_idle_forever();
            }
        }
    }
#else
    init_idle_forever();
#endif
}
