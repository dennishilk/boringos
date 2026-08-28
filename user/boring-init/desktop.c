#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/runtime.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

static void init_idle_forever(void) __attribute__((noreturn));
static void init_idle_forever(void) {
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static bool init_write_exact(const char *message, size_t length) {
    return boring_console_write(message, length) == (long)length;
}

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

static void init_fail(void) __attribute__((noreturn));
static void init_fail(void) {
    static const char failed[] = "boring-init: FAILED\n";
    (void)init_write_exact(failed, sizeof(failed) - 1U);
    init_idle_forever();
}

static long init_spawn(const char *path, size_t length) {
    const char *argv[1] = { path };
    const struct boring_spawn_stdio stdio_config = {
        BORING_FD_STDIN, BORING_FD_STDOUT, BORING_FD_STDERR, 0U
    };

    return boring_spawn(path, length, argv, 1U, &stdio_config);
}

static void init_wait_child(long child_pid, const char *label,
                            size_t label_length) {
    long waited;
    int status = 0;

    waited = boring_waitpid((uint64_t)child_pid, &status);
    if ((waited != child_pid) || (status != 0) ||
        !init_write_exact(label, label_length) ||
        !init_write_u64((uint64_t)(uint32_t)status) ||
        !init_write_exact("\n", 1U)) {
        init_fail();
    }
}

static void init_verify_session_drained(void) {
    struct boring_system_info info;

    if ((boring_system_info(&info) != 0L) ||
        (info.abi_version != BORING_SYSTEM_INFO_ABI_VERSION) ||
        (info.current_pid != 1ULL) || (info.process_count != 1U)) {
        init_fail();
    }
}

int boring_main(void) {
    static const char starting[] = "boring-init: starting\n";
    static const char pid_ok[] = "boring-init: pid 1\n";
    static const char online[] = "boring-init: online\n";
    static const char display_path[] = "/bin/boring-display";
    static const char wm_path[] = "/bin/boringwm";
    static const char display_spawned[] =
        "boring-init: desktop display spawned pid ";
    static const char wm_spawned[] =
        "boring-init: desktop WM spawned pid ";
    static const char wm_exited[] =
        "boring-init: desktop WM exited status ";
    static const char display_exited[] =
        "boring-init: desktop display exited status ";
    static const char drained[] =
        "boring-init: desktop session drained\n";
    const uint64_t pid = boring_getpid();
    long display_pid;
    long wm_pid;

    if (!init_write_exact(starting, sizeof(starting) - 1U) ||
        (pid != 1ULL) || !init_write_exact(pid_ok, sizeof(pid_ok) - 1U) ||
        !init_write_exact(online, sizeof(online) - 1U)) {
        init_fail();
    }

    display_pid = init_spawn(display_path, sizeof(display_path) - 1U);
    if ((display_pid <= 1L) ||
        !init_write_exact(display_spawned, sizeof(display_spawned) - 1U) ||
        !init_write_u64((uint64_t)display_pid) ||
        !init_write_exact("\n", 1U)) {
        init_fail();
    }

    wm_pid = init_spawn(wm_path, sizeof(wm_path) - 1U);
    if ((wm_pid <= display_pid) ||
        !init_write_exact(wm_spawned, sizeof(wm_spawned) - 1U) ||
        !init_write_u64((uint64_t)wm_pid) ||
        !init_write_exact("\n", 1U)) {
        init_fail();
    }

    init_wait_child(wm_pid, wm_exited, sizeof(wm_exited) - 1U);
    init_wait_child(display_pid, display_exited,
                    sizeof(display_exited) - 1U);
    init_verify_session_drained();
    if (!init_write_exact(drained, sizeof(drained) - 1U)) {
        init_fail();
    }
    init_idle_forever();
}
