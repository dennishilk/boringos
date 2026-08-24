#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/runtime.h>
#include <boring/syscall.h>

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

    init_idle_forever();
}
