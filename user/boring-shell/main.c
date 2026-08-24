#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/runtime.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define BORING_SHELL_LINE_MAX 128U

static void shell_idle_forever(void) __attribute__((noreturn));
static void shell_idle_forever(void) {
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static bool shell_write(const char *buffer, size_t length) {
    size_t offset = 0U;

    if ((buffer == NULL) && (length != 0U)) {
        return false;
    }
    while (offset < length) {
        size_t chunk = length - offset;
        long result;

        if (chunk > (size_t)BORING_SYSCALL_CONSOLE_IO_MAX) {
            chunk = (size_t)BORING_SYSCALL_CONSOLE_IO_MAX;
        }
        result = boring_console_write(&buffer[offset], chunk);
        if (result != (long)chunk) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

static bool shell_write_text(const char *text) {
    return (text != NULL) && shell_write(text, strlen(text));
}

static bool shell_is_space(char character) {
    return (character == ' ') || (character == '\t');
}

static bool shell_text_equals(const char *left, const char *right) {
    size_t index = 0U;

    if ((left == NULL) || (right == NULL)) {
        return false;
    }
    for (;;) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == '\0') {
            return true;
        }
        ++index;
    }
}

static bool shell_syscall_safety(void) {
    static const char dot[] = ".";
    static const char dotdot[] = "..";
    static const char slash_name[] = "a/b";
    static const char missing[] = "missing";
    static const char embedded_nul[3] = { 'x', '\0', 'y' };
    const char *const kernel_pointer =
        (const char *)(uintptr_t)0xffff800000000000ULL;
    const char *const overflow_pointer =
        (const char *)(uintptr_t)(UINTPTR_MAX - 1U);

    return (boring_fs_mkdir(NULL, 1U) == -(long)BORING_SYSCALL_EFAULT) &&
           (boring_fs_mkdir(kernel_pointer, 1U) ==
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_fs_mkdir(overflow_pointer, 4U) ==
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_fs_mkdir(dot, 0U) == -(long)BORING_SYSCALL_EINVAL) &&
           (boring_fs_mkdir(dot, sizeof(dot) - 1U) ==
            -(long)BORING_SYSCALL_EINVAL) &&
           (boring_fs_mkdir(dotdot, sizeof(dotdot) - 1U) ==
            -(long)BORING_SYSCALL_EINVAL) &&
           (boring_fs_mkdir(slash_name, sizeof(slash_name) - 1U) ==
            -(long)BORING_SYSCALL_EINVAL) &&
           (boring_fs_mkdir(dot, 256U) ==
            -(long)BORING_SYSCALL_ENAMETOOLONG) &&
           (boring_fs_mkdir(embedded_nul, sizeof(embedded_nul)) ==
            -(long)BORING_SYSCALL_EINVAL) &&
           (boring_fs_rmdir(missing, sizeof(missing) - 1U) ==
            -(long)BORING_SYSCALL_ENOENT) &&
           (boring_fs_chdir(missing, sizeof(missing) - 1U) ==
            -(long)BORING_SYSCALL_ENOENT) &&
           (boring_fs_readdir(dot, sizeof(dot) - 1U, 0ULL, NULL) ==
            -(long)BORING_SYSCALL_EFAULT);
}

static bool shell_read_line(char *line, size_t capacity) {
    size_t length = 0U;
    bool overflow = false;

    if ((line == NULL) || (capacity < 2U)) {
        return false;
    }

    for (;;) {
        char character = '\0';
        const long result = boring_console_read(&character, 1U);

        if (result != 1L) {
            return false;
        }
        if ((character == '\n') || (character == '\r')) {
            if (!shell_write("\n", 1U)) {
                return false;
            }
            if (overflow) {
                line[0] = '\0';
                if (!shell_write_text("shell: line too long\n")) {
                    return false;
                }
                return true;
            }
            line[length] = '\0';
            return true;
        }
        if ((character == '\b') || ((unsigned char)character == 0x7fU)) {
            if (!overflow && (length != 0U)) {
                --length;
                if (!shell_write("\b \b", 3U)) {
                    return false;
                }
            }
            continue;
        }
        if (((character >= ' ') && (character <= '~')) ||
            (character == '\t')) {
            if (!overflow) {
                if (length >= capacity - 1U) {
                    overflow = true;
                } else {
                    line[length] = character;
                    ++length;
                }
            }
            if (!shell_write(&character, 1U)) {
                return false;
            }
        }
    }
}

static char *shell_trim(char *text) {
    char *start = text;
    char *end;

    if (text == NULL) {
        return NULL;
    }
    while ((*start != '\0') && shell_is_space(*start)) {
        ++start;
    }
    end = start;
    while (*end != '\0') {
        ++end;
    }
    while ((end > start) && shell_is_space(end[-1])) {
        --end;
    }
    *end = '\0';
    return start;
}

static bool shell_split(char *line, char **command_out, char **argument_out) {
    char *command;
    char *cursor;
    char *argument;
    char *scan;

    if ((line == NULL) || (command_out == NULL) || (argument_out == NULL)) {
        return false;
    }
    command = shell_trim(line);
    if (command == NULL) {
        return false;
    }
    cursor = command;
    while ((*cursor != '\0') && !shell_is_space(*cursor)) {
        ++cursor;
    }
    if (*cursor == '\0') {
        *command_out = command;
        *argument_out = cursor;
        return true;
    }
    *cursor = '\0';
    ++cursor;
    while ((*cursor != '\0') && shell_is_space(*cursor)) {
        ++cursor;
    }
    argument = shell_trim(cursor);
    if (argument == NULL) {
        return false;
    }
    scan = argument;
    while ((*scan != '\0') && !shell_is_space(*scan)) {
        ++scan;
    }
    if (*scan != '\0') {
        char *rest = scan;
        while ((*rest != '\0') && shell_is_space(*rest)) {
            ++rest;
        }
        if (*rest != '\0') {
            return false;
        }
        *scan = '\0';
    }
    *command_out = command;
    *argument_out = argument;
    return true;
}

static bool shell_print_fs_error(const char *command, long result) {
    const char *message = "error";

    if (result == -(long)BORING_SYSCALL_ENOENT) {
        message = "not found";
    } else if (result == -(long)BORING_SYSCALL_EEXIST) {
        message = "already exists";
    } else if (result == -(long)BORING_SYSCALL_ENOTDIR) {
        message = "not a directory";
    } else if (result == -(long)BORING_SYSCALL_ENOTEMPTY) {
        message = "directory not empty";
    } else if (result == -(long)BORING_SYSCALL_ENOSPC) {
        message = "no space";
    } else if (result == -(long)BORING_SYSCALL_EBUSY) {
        message = "busy";
    } else if (result == -(long)BORING_SYSCALL_ENAMETOOLONG) {
        message = "name too long";
    } else if (result == -(long)BORING_SYSCALL_EACCES) {
        message = "access denied";
    } else if (result == -(long)BORING_SYSCALL_ENOTSUP) {
        message = "not supported";
    } else if (result == -(long)BORING_SYSCALL_EINVAL) {
        message = "invalid argument";
    }

    return shell_write_text(command) && shell_write_text(": ") &&
           shell_write_text(message) && shell_write_text("\n");
}

static bool shell_command_help(const char *argument) {
    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("help: usage: help\n");
    }
    return shell_write_text("help\n") &&
           shell_write_text("ls [path]\n") &&
           shell_write_text("mkdir <name>\n") &&
           shell_write_text("rmdir <name>\n") &&
           shell_write_text("cd <path>\n");
}

static bool shell_command_ls(const char *argument) {
    static const char dot[] = ".";
    const char *path = argument;
    size_t path_length;
    uint64_t index = 0ULL;

    if ((path == NULL) || (path[0] == '\0')) {
        path = dot;
    }
    path_length = strlen(path);
    for (;;) {
        struct boring_dirent entry;
        const long result = boring_fs_readdir(path, path_length, index, &entry);

        if (result == 0L) {
            return true;
        }
        if (result < 0L) {
            return shell_print_fs_error("ls", result);
        }
        if ((result != 1L) ||
            (entry.name_length >= (uint32_t)BORING_DIRENT_NAME_CAPACITY) ||
            (entry.name[entry.name_length] != '\0')) {
            return false;
        }
        if (!shell_write(entry.name, (size_t)entry.name_length) ||
            !shell_write("\n", 1U)) {
            return false;
        }
        ++index;
    }
}

static bool shell_command_mkdir(const char *argument) {
    long result;

    if ((argument == NULL) || (argument[0] == '\0')) {
        return shell_write_text("mkdir: usage: mkdir <name>\n");
    }
    result = boring_fs_mkdir(argument, strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("mkdir", result);
}

static bool shell_command_rmdir(const char *argument) {
    long result;

    if ((argument == NULL) || (argument[0] == '\0')) {
        return shell_write_text("rmdir: usage: rmdir <name>\n");
    }
    result = boring_fs_rmdir(argument, strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("rmdir", result);
}

static bool shell_command_cd(const char *argument) {
    long result;

    if ((argument == NULL) || (argument[0] == '\0')) {
        return shell_write_text("cd: usage: cd <path>\n");
    }
    result = boring_fs_chdir(argument, strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("cd", result);
}

static bool shell_execute_line(char *line) {
    char *command = NULL;
    char *argument = NULL;

    if (!shell_split(line, &command, &argument)) {
        return shell_write_text("shell: too many arguments\n");
    }
    if ((command == NULL) || (command[0] == '\0')) {
        return true;
    }
    if (shell_text_equals(command, "help")) {
        return shell_command_help(argument);
    }
    if (shell_text_equals(command, "ls")) {
        return shell_command_ls(argument);
    }
    if (shell_text_equals(command, "mkdir")) {
        return shell_command_mkdir(argument);
    }
    if (shell_text_equals(command, "rmdir")) {
        return shell_command_rmdir(argument);
    }
    if (shell_text_equals(command, "cd")) {
        return shell_command_cd(argument);
    }

    return shell_write_text("command not found: ") &&
           shell_write_text(command) && shell_write_text("\n");
}

int boring_main(void) {
    static const char starting[] = "boring-shell: starting\n";
    static const char pid_ok[] = "boring-shell: pid 2\n";
    static const char ready[] = "boring-shell ready.\n\n";
    static const char failed[] = "boring-shell: FAILED\n";
    char line[BORING_SHELL_LINE_MAX + 1U];

    if (!shell_write(starting, sizeof(starting) - 1U) ||
        (boring_getpid() != 2ULL) ||
        !shell_write(pid_ok, sizeof(pid_ok) - 1U) ||
        !shell_syscall_safety() ||
        !shell_write(ready, sizeof(ready) - 1U)) {
        (void)shell_write(failed, sizeof(failed) - 1U);
        shell_idle_forever();
    }

    for (;;) {
        if (!shell_write_text("boring> ") ||
            !shell_read_line(line, sizeof(line)) ||
            !shell_execute_line(line)) {
            (void)shell_write(failed, sizeof(failed) - 1U);
            shell_idle_forever();
        }
    }
}
