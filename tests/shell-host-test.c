#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define boring_main boring_shell_embedded_main
#include "../user/boring-shell/main.c"
#undef boring_main

struct mock_dirent {
    const char *path;
    const char *name;
    uint32_t type;
};

static const struct mock_dirent mock_dirents[] = {
    { ".", "TEST", BORING_DIRENT_TYPE_DIRECTORY },
    { ".", "TEAM", BORING_DIRENT_TYPE_DIRECTORY },
    { ".", "README.txt", BORING_DIRENT_TYPE_REGULAR },
    { ".", "alpha-one", BORING_DIRENT_TYPE_REGULAR },
    { ".", "alpha-two", BORING_DIRENT_TYPE_REGULAR },
    { "/bin", "boringfetch", BORING_DIRENT_TYPE_REGULAR },
    { "/bin", "cat", BORING_DIRENT_TYPE_REGULAR }
};

static const char *mock_input;
static size_t mock_input_length;
static size_t mock_input_offset;
static char mock_output[65536];
static size_t mock_output_length;
static char mock_written_path[BORING_SHELL_LINE_MAX + 1U];
static unsigned char mock_written_bytes[BORING_SHELL_LINE_MAX + 1U];
static size_t mock_written_length;
static bool mock_timer_enabled;
static char mock_launch_path[BORING_SYSCALL_EXEC_PATH_MAX + 1U];
static char mock_launch_argv[BORING_SYSCALL_ARG_MAX][BORING_SHELL_LINE_MAX + 1U];
static size_t mock_launch_argc;
static uint64_t mock_wait_pid;

static void test_fail(const char *message) {
    (void)fprintf(stderr, "shell host test failed: %s\n", message);
    exit(1);
}

static void test_require(bool condition, const char *message) {
    if (!condition) {
        test_fail(message);
    }
}

static void mock_begin(const char *input) {
    mock_input = input;
    mock_input_length = strlen(input);
    mock_input_offset = 0U;
    mock_output_length = 0U;
    mock_output[0] = '\0';
    shell_skip_lf_after_cr = false;
}

static void expect_line(const char *input,
                        size_t capacity,
                        const char *expected,
                        const char *message) {
    char line[BORING_SHELL_LINE_MAX + 1U];

    mock_begin(input);
    test_require(capacity <= sizeof(line), "invalid test capacity");
    test_require(shell_read_line("test$ ", line, capacity), message);
    test_require(strcmp(line, expected) == 0, message);
    test_require(mock_input_offset == mock_input_length,
                 "editor left unread input bytes");
}

size_t boring_strlen(const char *text) {
    return strlen(text);
}

uint64_t boring_getpid(void) {
    return 2ULL;
}

long boring_console_write(const void *buffer, size_t length) {
    if ((buffer == NULL) ||
        (length > sizeof(mock_output) - mock_output_length - 1U)) {
        return -(long)BORING_SYSCALL_EFAULT;
    }
    (void)memcpy(&mock_output[mock_output_length], buffer, length);
    mock_output_length += length;
    mock_output[mock_output_length] = '\0';
    return (long)length;
}

long boring_console_read(void *buffer, size_t length) {
    if ((buffer == NULL) || (length != 1U) ||
        (mock_input_offset >= mock_input_length)) {
        return -(long)BORING_SYSCALL_EFAULT;
    }
    *(char *)buffer = mock_input[mock_input_offset];
    ++mock_input_offset;
    return 1L;
}

long boring_fs_readdir(const char *path,
                       size_t path_length,
                       uint64_t index,
                       struct boring_dirent *entry) {
    uint64_t logical = 0ULL;
    size_t candidate;

    if ((path == NULL) || (entry == NULL) ||
        (strlen(path) != path_length)) {
        return -(long)BORING_SYSCALL_EINVAL;
    }
    for (candidate = 0U;
         candidate < sizeof(mock_dirents) / sizeof(mock_dirents[0]);
         ++candidate) {
        const struct mock_dirent *const source = &mock_dirents[candidate];
        const size_t name_length = strlen(source->name);

        if (strcmp(source->path, path) != 0) {
            continue;
        }
        if (logical == index) {
            (void)memset(entry, 0, sizeof(*entry));
            entry->node_id = (uint64_t)candidate + 1ULL;
            entry->type = source->type;
            entry->name_length = (uint32_t)name_length;
            (void)memcpy(entry->name, source->name, name_length + 1U);
            return 1L;
        }
        ++logical;
    }
    return 0L;
}

long boring_fs_mkdir(const char *name, size_t length) {
    (void)name;
    (void)length;
    return 0L;
}

long boring_fs_rmdir(const char *name, size_t length) {
    (void)name;
    (void)length;
    return 0L;
}

long boring_fs_chdir(const char *path, size_t length) {
    (void)path;
    (void)length;
    return 0L;
}

long boring_fs_read(const char *path,
                    size_t path_length,
                    uint64_t offset,
                    void *buffer,
                    size_t capacity) {
    (void)path;
    (void)path_length;
    (void)offset;
    (void)buffer;
    (void)capacity;
    return 0L;
}

long boring_fs_touch(const char *path, size_t length) {
    (void)path;
    (void)length;
    return 0L;
}

long boring_fs_write(const char *path,
                     size_t path_length,
                     const void *buffer,
                     size_t length) {
    if ((path == NULL) || (buffer == NULL) ||
        (strlen(path) != path_length) ||
        (path_length >= sizeof(mock_written_path)) ||
        (length > sizeof(mock_written_bytes))) {
        return -(long)BORING_SYSCALL_EINVAL;
    }
    (void)memcpy(mock_written_path, path, path_length + 1U);
    (void)memcpy(mock_written_bytes, buffer, length);
    mock_written_length = length;
    return (long)length;
}

long boring_fs_unlink(const char *path, size_t length) {
    (void)path;
    (void)length;
    return 0L;
}

long boring_launch_argv(const char *path,
                        size_t path_length,
                        const char *const argv[],
                        size_t argc) {
    size_t index;

    if ((path == NULL) || (argv == NULL) ||
        (path_length >= sizeof(mock_launch_path)) ||
        (strlen(path) != path_length) ||
        (argc == 0U) || (argc > (size_t)BORING_SYSCALL_ARG_MAX)) {
        return -(long)BORING_SYSCALL_EINVAL;
    }
    (void)memcpy(mock_launch_path, path, path_length + 1U);
    mock_launch_argc = argc;
    for (index = 0U; index < argc; ++index) {
        const size_t length = (argv[index] != NULL) ?
            strlen(argv[index]) : sizeof(mock_launch_argv[index]);

        if ((argv[index] == NULL) ||
            (length >= sizeof(mock_launch_argv[index]))) {
            return -(long)BORING_SYSCALL_EINVAL;
        }
        (void)memcpy(mock_launch_argv[index], argv[index], length + 1U);
    }
    return 3L;
}

long boring_waitpid(uint64_t pid, int *status) {
    if ((pid != 3ULL) || (status == NULL)) {
        return -(long)BORING_SYSCALL_EINVAL;
    }
    mock_wait_pid = pid;
    *status = 0;
    return 3L;
}

long boring_system_info(struct boring_system_info *info) {
    if (info == NULL) {
        return -(long)BORING_SYSCALL_EFAULT;
    }
    (void)memset(info, 0, sizeof(*info));
    info->abi_version = BORING_SYSTEM_INFO_ABI_VERSION;
    (void)strcpy(info->hostname, "boringos");
    (void)strcpy(info->username, "boring");
    (void)strcpy(info->os_name, "BoringOS");
    (void)strcpy(info->kernel_name, "BoringKernel");
    (void)strcpy(info->kernel_version, "0.0.32-dev");
    (void)strcpy(info->arch, "x86_64");
    (void)strcpy(info->root_fs, "RAMFS");
    (void)strcpy(info->root_device, "memory");
    info->usable_memory_bytes = 128ULL * BORING_SHELL_MIB;
    info->free_memory_bytes = 120ULL * BORING_SHELL_MIB;
    info->process_count = 2U;
    info->current_pid = 2ULL;
    if (mock_timer_enabled) {
        info->uptime_ticks = 1234ULL;
        info->timer_frequency_millihz = 100000U;
    }
    return 0L;
}

long boring_getcwd(char *buffer, size_t capacity) {
    if ((buffer == NULL) || (capacity < 2U)) {
        return -(long)BORING_SYSCALL_EFAULT;
    }
    buffer[0] = '/';
    buffer[1] = '\0';
    return 1L;
}

long boring_process_snapshot(uint64_t index, struct boring_process_info *info) {
    (void)index;
    (void)info;
    return 0L;
}

void boring_exit(int status) {
    (void)status;
    abort();
}

int main(void) {
    size_t index;
    char write_line[] = "write note.txt exact-text";
    char write_no_newline[] = "write -n raw.txt exact-bytes";
    char external_fetch[] = "boringfetch";
    char external_fetch_arg[] = "boringfetch extra";
    char external_cat[] = "cat README.txt";

    shell_history_reset();
    expect_line("plain text\n", sizeof(shell_history_draft), "plain text",
                "printable input");
    expect_line("boringfeth\033[Dc\n", sizeof(shell_history_draft),
                "boringfetch", "left-arrow insertion");
    expect_line("helpp\b\n", sizeof(shell_history_draft), "help",
                "backspace deletion");
    expect_line("helXp\033[D\033[D\033[3~\n", sizeof(shell_history_draft),
                "help", "forward delete");
    expect_line("hlp\033[H\033[Ce\n", sizeof(shell_history_draft), "help",
                "right-arrow insertion");
    expect_line("elp\033[Hh\n", sizeof(shell_history_draft), "help",
                "home insertion");
    expect_line("elpX\033[Hh\033[F\b\n", sizeof(shell_history_draft), "help",
                "end positioning");
    expect_line("help\033[123456789~\n", sizeof(shell_history_draft), "help",
                "bounded unknown CSI consumption");

    shell_history_reset();
    test_require(shell_history_add("echo one"), "history add one");
    test_require(shell_history_add("echo two"), "history add two");
    expect_line("\033[A\n", sizeof(shell_history_draft), "echo two",
                "history up");
    expect_line("\033[A\033[A\n", sizeof(shell_history_draft), "echo one",
                "history older");
    expect_line("draft\033[A\033[B\n", sizeof(shell_history_draft), "draft",
                "history down restores draft");

    shell_history_reset();
    expect_line("boringf\t\n", sizeof(shell_history_draft), "boringfetch ",
                "single command completion");
    expect_line("cd TES\t\n", sizeof(shell_history_draft), "cd TEST/",
                "directory completion");
    expect_line("cat REA\t\n", sizeof(shell_history_draft),
                "cat README.txt ", "regular-file completion");
    expect_line("cat a\tone\n", sizeof(shell_history_draft), "cat alpha-one",
                "multiple-match common prefix");
    expect_line("cat Z\t\n", sizeof(shell_history_draft), "cat Z",
                "zero-match completion");
    expect_line("cat REA\t\n", 12U, "cat REA",
                "completion buffer boundary");

    shell_history_reset();
    for (index = 0U; index < 20U; ++index) {
        char command[16];
        const int written = snprintf(command, sizeof(command), "cmd%02zu", index);

        test_require((written > 0) && ((size_t)written < sizeof(command)),
                     "history fixture formatting");
        test_require(shell_history_add(command), "bounded history add");
    }
    test_require(shell_history_count == BORING_SHELL_HISTORY_CAPACITY,
                 "history capacity");
    test_require(shell_history_total == 20ULL, "history total");
    test_require(strcmp(shell_history[shell_history_slot(0U)], "cmd04") == 0,
                 "history oldest retained entry");
    test_require(strcmp(shell_history[shell_history_slot(
                            BORING_SHELL_HISTORY_CAPACITY - 1U)],
                        "cmd19") == 0,
                 "history newest retained entry");

    mock_written_length = 0U;
    test_require(shell_execute_line(write_line), "default write execution");
    test_require(strcmp(mock_written_path, "note.txt") == 0,
                 "default write path");
    test_require(mock_written_length == sizeof("exact-text\n") - 1U,
                 "default write length");
    test_require(memcmp(mock_written_bytes, "exact-text\n",
                        sizeof("exact-text\n") - 1U) == 0,
                 "default write appends exactly one newline");

    mock_written_length = 0U;
    test_require(shell_execute_line(write_no_newline), "write -n execution");
    test_require(strcmp(mock_written_path, "raw.txt") == 0,
                 "write -n path");
    test_require(mock_written_length == sizeof("exact-bytes") - 1U,
                 "write -n length");
    test_require(memcmp(mock_written_bytes, "exact-bytes",
                        sizeof("exact-bytes") - 1U) == 0,
                 "write -n preserves exact bytes");

    mock_output_length = 0U;
    mock_output[0] = '\0';
    test_require(shell_print_fs_error("rm", -(long)BORING_SYSCALL_EISDIR),
                 "EISDIR mapping execution");
    test_require(strcmp(mock_output, "rm: is a directory\r\n") == 0,
                 "central EISDIR user-facing mapping");

    mock_launch_path[0] = '\0';
    mock_launch_argc = 0U;
    mock_wait_pid = 0ULL;
    test_require(shell_execute_line(external_fetch),
                 "external boringfetch execution");
    test_require(strcmp(mock_launch_path, "/bin/boringfetch") == 0,
                 "external command resolves fixed /bin path");
    test_require(mock_launch_argc == 1U,
                 "external command argc");
    test_require(strcmp(mock_launch_argv[0], "boringfetch") == 0,
                 "external command argv0");
    test_require(mock_wait_pid == 3ULL,
                 "external command waitpid");

    mock_launch_path[0] = '\0';
    mock_launch_argc = 0U;
    mock_wait_pid = 0ULL;
    test_require(shell_execute_line(external_fetch_arg),
                 "external boringfetch argv execution");
    test_require(strcmp(mock_launch_path, "/bin/boringfetch") == 0,
                 "external argv command resolves /bin path");
    test_require(mock_launch_argc == 2U,
                 "external argv argc");
    test_require(strcmp(mock_launch_argv[0], "boringfetch") == 0 &&
                 strcmp(mock_launch_argv[1], "extra") == 0,
                 "external argv content");
    test_require(mock_wait_pid == 3ULL,
                 "external argv waitpid");

    mock_launch_path[0] = '\0';
    mock_launch_argc = 0U;
    mock_wait_pid = 0ULL;
    test_require(shell_execute_line(external_cat),
                 "external cat execution");
    test_require(strcmp(mock_launch_path, "/bin/cat") == 0,
                 "cat resolves fixed /bin path");
    test_require(mock_launch_argc == 2U &&
                 strcmp(mock_launch_argv[0], "cat") == 0 &&
                 strcmp(mock_launch_argv[1], "README.txt") == 0,
                 "cat argv is forwarded to the standalone program");
    test_require(mock_wait_pid == 3ULL,
                 "external cat waitpid");

    (void)puts("BoringOS shell host editor/completion tests passed.");
    return 0;
}
