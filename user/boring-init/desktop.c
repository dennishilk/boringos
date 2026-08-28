#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/runtime.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

enum init_session_state {
    INIT_SESSION_STARTING = 0,
    INIT_SESSION_RUNNING,
    INIT_SESSION_DRAINING,
    INIT_SESSION_FAILED,
    INIT_SESSION_DRAINED
};

struct init_session_child {
    long pid;
    int status;
    bool running;
    bool exited;
    bool reaped;
};

struct init_desktop_session {
    enum init_session_state state;
    struct init_session_child display;
    struct init_session_child wm;
    bool failed;
};

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

static bool init_write_status(const char *label, size_t label_length,
                              int status) {
    if ((label == NULL) ||
        !init_write_exact(label, label_length) ||
        !init_write_u64((uint64_t)(uint32_t)status) ||
        !init_write_exact("\n", 1U)) {
        return false;
    }
    return true;
}

static bool init_write_state(enum init_session_state state) {
    switch (state) {
        case INIT_SESSION_STARTING:
            return init_write_exact(
                "boring-init: desktop session state STARTING\n",
                sizeof("boring-init: desktop session state STARTING\n") - 1U);
        case INIT_SESSION_RUNNING:
            return init_write_exact(
                "boring-init: desktop session state RUNNING\n",
                sizeof("boring-init: desktop session state RUNNING\n") - 1U);
        case INIT_SESSION_DRAINING:
            return init_write_exact(
                "boring-init: desktop session state DRAINING\n",
                sizeof("boring-init: desktop session state DRAINING\n") - 1U);
        case INIT_SESSION_FAILED:
            return init_write_exact(
                "boring-init: desktop session state FAILED\n",
                sizeof("boring-init: desktop session state FAILED\n") - 1U);
        case INIT_SESSION_DRAINED:
            return init_write_exact(
                "boring-init: desktop session state DRAINED\n",
                sizeof("boring-init: desktop session state DRAINED\n") - 1U);
        default:
            return false;
    }
}

static void init_set_state(struct init_desktop_session *session,
                           enum init_session_state state) {
    if ((session == NULL) || !init_write_state(state)) {
        init_fail();
    }
    session->state = state;
}

static void init_mark_failed(struct init_desktop_session *session,
                             const char *child_name, size_t child_name_length,
                             int status) {
    static const char prefix[] = "boring-init: desktop session failed: ";
    static const char middle[] = " exited status ";

    if ((session == NULL) || session->failed || (child_name == NULL) ||
        !init_write_exact(prefix, sizeof(prefix) - 1U) ||
        !init_write_exact(child_name, child_name_length) ||
        !init_write_exact(middle, sizeof(middle) - 1U) ||
        !init_write_u64((uint64_t)(uint32_t)status) ||
        !init_write_exact("\n", 1U)) {
        init_fail();
    }
    session->failed = true;
    init_set_state(session, INIT_SESSION_FAILED);
    init_set_state(session, INIT_SESSION_DRAINING);
}

static void init_record_reap(struct init_session_child *child, int status) {
    if ((child == NULL) || !child->running || child->exited || child->reaped) {
        init_fail();
    }
    child->running = false;
    child->exited = true;
    child->status = status;
    child->reaped = true;
}

static bool init_both_reaped(const struct init_desktop_session *session) {
    return (session != NULL) && session->display.reaped && session->wm.reaped;
}

static void init_supervise_session(struct init_desktop_session *session) {
    static const char wm_exited[] =
        "boring-init: desktop WM exited status ";
    static const char display_exited[] =
        "boring-init: desktop display exited status ";

    if ((session == NULL) || (session->state != INIT_SESSION_RUNNING)) {
        init_fail();
    }
    while (!init_both_reaped(session)) {
        int status = 0;
        const long waited = boring_waitpid(0ULL, &status);
        struct init_session_child *child;
        const char *label;
        size_t label_length;
        const char *failure_name;
        size_t failure_name_length;
        bool display_child;

        if (waited == session->display.pid) {
            child = &session->display;
            label = display_exited;
            label_length = sizeof(display_exited) - 1U;
            failure_name = "display";
            failure_name_length = sizeof("display") - 1U;
            display_child = true;
        } else if (waited == session->wm.pid) {
            child = &session->wm;
            label = wm_exited;
            label_length = sizeof(wm_exited) - 1U;
            failure_name = "WM";
            failure_name_length = sizeof("WM") - 1U;
            display_child = false;
        } else {
            init_fail();
        }

        init_record_reap(child, status);
        if (!init_write_status(label, label_length, status)) {
            init_fail();
        }

        if (!session->failed) {
            const bool unexpected_display_first =
                display_child && !session->wm.reaped;

            if ((status != 0) || unexpected_display_first) {
                init_mark_failed(session, failure_name,
                                 failure_name_length, status);
            } else if (!init_both_reaped(session)) {
                init_set_state(session, INIT_SESSION_DRAINING);
            }
        }
    }
    init_set_state(session, INIT_SESSION_DRAINED);
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
    static const char drained[] =
        "boring-init: desktop session drained\n";
    static const char failed_drained[] =
        "boring-init: desktop failed session drained\n";
    const uint64_t pid = boring_getpid();
    struct init_desktop_session session = {0};

    session.state = INIT_SESSION_STARTING;
    if (!init_write_exact(starting, sizeof(starting) - 1U) ||
        (pid != 1ULL) || !init_write_exact(pid_ok, sizeof(pid_ok) - 1U) ||
        !init_write_exact(online, sizeof(online) - 1U) ||
        !init_write_state(session.state)) {
        init_fail();
    }

    session.display.pid = init_spawn(display_path, sizeof(display_path) - 1U);
    if ((session.display.pid <= 1L) ||
        !init_write_exact(display_spawned, sizeof(display_spawned) - 1U) ||
        !init_write_u64((uint64_t)session.display.pid) ||
        !init_write_exact("\n", 1U)) {
        init_fail();
    }
    session.display.running = true;

    session.wm.pid = init_spawn(wm_path, sizeof(wm_path) - 1U);
    if ((session.wm.pid <= 1L) || (session.wm.pid == session.display.pid) ||
        !init_write_exact(wm_spawned, sizeof(wm_spawned) - 1U) ||
        !init_write_u64((uint64_t)session.wm.pid) ||
        !init_write_exact("\n", 1U)) {
        init_fail();
    }
    session.wm.running = true;
    init_set_state(&session, INIT_SESSION_RUNNING);

    init_supervise_session(&session);
    init_verify_session_drained();
    if (session.failed) {
        if (!init_write_exact(failed_drained, sizeof(failed_drained) - 1U)) {
            init_fail();
        }
    } else if (!init_write_exact(drained, sizeof(drained) - 1U)) {
        init_fail();
    }
    init_idle_forever();
}
