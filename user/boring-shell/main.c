#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/runtime.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define BORING_SHELL_LINE_MAX 512U
#define BORING_SHELL_ESCAPE_MAX 8U

static volatile uint64_t shell_state;
static bool shell_input_pushback_valid;
static char shell_input_pushback;
static bool shell_skip_lf_after_cr;

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
    return (text != NULL) && shell_write(text, boring_strlen(text));
}

static bool shell_write_u64(uint64_t value) {
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
    return shell_write(digits, count);
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
           (boring_system_info(NULL) == -(long)BORING_SYSCALL_EFAULT) &&
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
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_fs_touch(NULL, 1U) ==
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_fs_touch(dot, sizeof(dot) - 1U) ==
            -(long)BORING_SYSCALL_EINVAL) &&
           (boring_fs_read(missing, sizeof(missing) - 1U, 0ULL, NULL, 1U) ==
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_fs_write(missing, sizeof(missing) - 1U, NULL, 1U) ==
            -(long)BORING_SYSCALL_EFAULT) &&
           (boring_fs_unlink(missing, sizeof(missing) - 1U) ==
            -(long)BORING_SYSCALL_ENOENT);
}

static bool shell_input_read(char *character_out) {
    long result;

    if (character_out == NULL) {
        return false;
    }
    if (shell_input_pushback_valid) {
        *character_out = shell_input_pushback;
        shell_input_pushback_valid = false;
        return true;
    }
    result = boring_console_read(character_out, 1U);
    return result == 1L;
}

static void shell_input_unread(char character) {
    if (!shell_input_pushback_valid) {
        shell_input_pushback = character;
        shell_input_pushback_valid = true;
    }
}

static bool shell_cursor_left(size_t count) {
    char digits[21];
    size_t digits_count = 0U;
    size_t index;

    if (count == 0U) {
        return true;
    }
    do {
        digits[digits_count] = (char)('0' + (char)(count % 10U));
        count /= 10U;
        ++digits_count;
    } while (count != 0U);
    if (!shell_write("\x1b[", 2U)) {
        return false;
    }
    for (index = 0U; index < digits_count; ++index) {
        const char digit = digits[digits_count - index - 1U];
        if (!shell_write(&digit, 1U)) {
            return false;
        }
    }
    return shell_write("D", 1U);
}

static bool shell_redraw_line(const char *prompt,
                              const char *line,
                              size_t length,
                              size_t cursor) {
    if ((prompt == NULL) || (line == NULL) || (cursor > length)) {
        return false;
    }
    return shell_write("\r", 1U) &&
           shell_write_text(prompt) &&
           shell_write(line, length) &&
           shell_write("\x1b[K", 3U) &&
           shell_cursor_left(length - cursor);
}

static bool shell_escape_equals(const char *sequence,
                                size_t length,
                                const char *expected) {
    size_t index = 0U;

    if ((sequence == NULL) || (expected == NULL)) {
        return false;
    }
    while (expected[index] != '\0') {
        if ((index >= length) || (sequence[index] != expected[index])) {
            return false;
        }
        ++index;
    }
    return index == length;
}

static bool shell_handle_escape(const char *prompt,
                                char *line,
                                size_t length,
                                size_t *cursor) {
    char first;
    char sequence[BORING_SHELL_ESCAPE_MAX];
    size_t sequence_length = 0U;

    if ((prompt == NULL) || (line == NULL) || (cursor == NULL) ||
        (*cursor > length) || !shell_input_read(&first)) {
        return false;
    }

    if (first == 'O') {
        char final;
        if (!shell_input_read(&final)) {
            return false;
        }
        if (final == 'H') {
            *cursor = 0U;
            return shell_redraw_line(prompt, line, length, *cursor);
        }
        if (final == 'F') {
            *cursor = length;
            return shell_redraw_line(prompt, line, length, *cursor);
        }
        return true;
    }

    if (first != '[') {
        shell_input_unread(first);
        return true;
    }

    while (sequence_length < (size_t)BORING_SHELL_ESCAPE_MAX) {
        char character;
        const unsigned char byte_min = 0x40U;
        const unsigned char byte_max = 0x7eU;
        const unsigned char byte =
            shell_input_read(&character) ? (unsigned char)character : 0U;

        if (byte == 0U) {
            return false;
        }
        sequence[sequence_length] = character;
        ++sequence_length;
        if ((byte >= byte_min) && (byte <= byte_max)) {
            break;
        }
    }

    if (shell_escape_equals(sequence, sequence_length, "D")) {
        if (*cursor != 0U) {
            --(*cursor);
            return shell_write("\x1b[D", 3U);
        }
        return true;
    }
    if (shell_escape_equals(sequence, sequence_length, "C")) {
        if (*cursor < length) {
            ++(*cursor);
            return shell_write("\x1b[C", 3U);
        }
        return true;
    }
    if (shell_escape_equals(sequence, sequence_length, "H") ||
        shell_escape_equals(sequence, sequence_length, "1~") ||
        shell_escape_equals(sequence, sequence_length, "7~")) {
        *cursor = 0U;
        return shell_redraw_line(prompt, line, length, *cursor);
    }
    if (shell_escape_equals(sequence, sequence_length, "F") ||
        shell_escape_equals(sequence, sequence_length, "4~") ||
        shell_escape_equals(sequence, sequence_length, "8~")) {
        *cursor = length;
        return shell_redraw_line(prompt, line, length, *cursor);
    }

    /* Up/Down and Delete are deliberately decoded but ignored in this first
     * reliability step. They must never leak printable suffix bytes into the
     * command. History/Delete semantics are added after this corruption gate.
     */
    return true;
}

static bool shell_read_line(const char *prompt, char *line, size_t capacity) {
    size_t length = 0U;
    size_t cursor = 0U;
    bool overflow = false;

    if ((prompt == NULL) || (line == NULL) || (capacity < 2U)) {
        return false;
    }

    line[0] = '\0';
    for (;;) {
        char character = '\0';
        size_t index;

        if (!shell_input_read(&character)) {
            return false;
        }
        if (shell_skip_lf_after_cr) {
            shell_skip_lf_after_cr = false;
            if (character == '\n') {
                continue;
            }
        }
        if ((unsigned char)character == 0x1bU) {
            if (!shell_handle_escape(prompt, line, length, &cursor)) {
                return false;
            }
            continue;
        }
        if ((character == '\n') || (character == '\r')) {
            if (character == '\r') {
                shell_skip_lf_after_cr = true;
            }
            if (!shell_write("\r\n", 2U)) {
                return false;
            }
            if (overflow) {
                line[0] = '\0';
                if (!shell_write_text("shell: line too long\r\n")) {
                    return false;
                }
                return true;
            }
            line[length] = '\0';
            return true;
        }
        if ((character == '\b') || ((unsigned char)character == 0x7fU)) {
            if (!overflow && (cursor != 0U)) {
                for (index = cursor - 1U; index < length - 1U; ++index) {
                    line[index] = line[index + 1U];
                }
                --cursor;
                --length;
                line[length] = '\0';
                if (!shell_redraw_line(prompt, line, length, cursor)) {
                    return false;
                }
            }
            continue;
        }
        if ((character >= ' ') && (character <= '~')) {
            if (!overflow) {
                if (length >= capacity - 1U) {
                    overflow = true;
                } else {
                    for (index = length; index > cursor; --index) {
                        line[index] = line[index - 1U];
                    }
                    line[cursor] = character;
                    ++cursor;
                    ++length;
                    line[length] = '\0';
                    if (cursor == length) {
                        if (!shell_write(&character, 1U)) {
                            return false;
                        }
                    } else if (!shell_redraw_line(prompt, line, length, cursor)) {
                        return false;
                    }
                }
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
    *command_out = command;
    *argument_out = argument;
    return true;
}

static bool shell_single_argument(const char *argument) {
    size_t index;

    if ((argument == NULL) || (argument[0] == '\0')) {
        return false;
    }
    for (index = 0U; argument[index] != '\0'; ++index) {
        if (shell_is_space(argument[index])) {
            return false;
        }
    }
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
           shell_write_text(message) && shell_write_text("\r\n");
}

static bool shell_command_help(const char *argument) {
    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("help: usage: help\r\n");
    }
    return shell_write_text("help\r\n") &&
           shell_write_text("ls [path]\r\n") &&
           shell_write_text("mkdir <name>\r\n") &&
           shell_write_text("rmdir <name>\r\n") &&
           shell_write_text("cd <path>\r\n") &&
           shell_write_text("cat <path>\r\n") &&
           shell_write_text("touch <path>\r\n") &&
           shell_write_text("write <path> <text>\r\n") &&
           shell_write_text("rm <path>\r\n");
}

static bool shell_command_ls(const char *argument) {
    static const char dot[] = ".";
    const char *path = argument;
    size_t path_length;
    uint64_t index = 0ULL;

    if ((path == NULL) || (path[0] == '\0')) {
        path = dot;
    } else if (!shell_single_argument(path)) {
        return shell_write_text("ls: usage: ls [path]\r\n");
    }
    path_length = boring_strlen(path);
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
            !shell_write("\r\n", 2U)) {
            return false;
        }
        ++index;
    }
}

static bool shell_command_mkdir(const char *argument) {
    long result;

    if (!shell_single_argument(argument)) {
        return shell_write_text("mkdir: usage: mkdir <name>\r\n");
    }
    result = boring_fs_mkdir(argument, boring_strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("mkdir", result);
}

static bool shell_command_rmdir(const char *argument) {
    long result;

    if (!shell_single_argument(argument)) {
        return shell_write_text("rmdir: usage: rmdir <name>\r\n");
    }
    result = boring_fs_rmdir(argument, boring_strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("rmdir", result);
}

static bool shell_command_cd(const char *argument) {
    long result;

    if (!shell_single_argument(argument)) {
        return shell_write_text("cd: usage: cd <path>\r\n");
    }
    result = boring_fs_chdir(argument, boring_strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("cd", result);
}

static bool shell_command_cat(const char *argument) {
    char buffer[BORING_SYSCALL_CONSOLE_IO_MAX];
    uint64_t offset = 0ULL;
    size_t path_length;

    if (!shell_single_argument(argument)) {
        return shell_write_text("cat: usage: cat <path>\r\n");
    }
    path_length = boring_strlen(argument);
    for (;;) {
        const long result = boring_fs_read(
            argument, path_length, offset, buffer, sizeof(buffer));

        if (result < 0L) {
            return shell_print_fs_error("cat", result);
        }
        if (result == 0L) {
            return true;
        }
        if ((size_t)result > sizeof(buffer)) {
            return false;
        }
        if (!shell_write(buffer, (size_t)result)) {
            return false;
        }
        offset += (uint64_t)result;
    }
}

static bool shell_command_touch(const char *argument) {
    long result;

    if (!shell_single_argument(argument)) {
        return shell_write_text("touch: usage: touch <path>\r\n");
    }
    result = boring_fs_touch(argument, boring_strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("touch", result);
}

static bool shell_command_write(char *argument) {
    char *path;
    char *cursor;
    char *text;
    size_t text_length;
    long result;

    if ((argument == NULL) || (argument[0] == '\0')) {
        return shell_write_text("write: usage: write <path> <text>\r\n");
    }
    path = argument;
    cursor = path;
    while ((*cursor != '\0') && !shell_is_space(*cursor)) {
        ++cursor;
    }
    if (*cursor == '\0') {
        return shell_write_text("write: usage: write <path> <text>\r\n");
    }
    *cursor = '\0';
    ++cursor;
    text = shell_trim(cursor);
    if ((text == NULL) || (text[0] == '\0')) {
        return shell_write_text("write: usage: write <path> <text>\r\n");
    }
    text_length = boring_strlen(text);
    result = boring_fs_write(path, boring_strlen(path), text, text_length);
    if (result == (long)text_length) {
        return true;
    }
    return (result < 0L) ? shell_print_fs_error("write", result) : false;
}

static bool shell_command_rm(const char *argument) {
    long result;

    if (!shell_single_argument(argument)) {
        return shell_write_text("rm: usage: rm <path>\r\n");
    }
    result = boring_fs_unlink(argument, boring_strlen(argument));
    if (result == 0L) {
        return true;
    }
    return shell_print_fs_error("rm", result);
}

static bool shell_command_boringfetch(const char *argument) {
    struct boring_system_info info;

    if ((argument == NULL) || (argument[0] != '\0')) {
        return shell_write_text("boringfetch: no arguments supported\r\n");
    }
    if ((boring_system_info(&info) != 0L) ||
        (info.abi_version != BORING_SYSTEM_INFO_ABI_VERSION)) {
        return shell_write_text("boringfetch: system information unavailable\r\n");
    }
    return shell_write_text("    ____             BoringOS\r\n") &&
           shell_write_text("   / __ )____  _____ -------------------------\r\n") &&
           shell_write_text("  / __  / __ \\/ ___/ OS: BoringOS\r\n") &&
           shell_write_text(" / /_/ / /_/ / /    Kernel: BoringKernel 0.0.27-dev\r\n") &&
           shell_write_text("/_____/\\____/_/      Arch: x86_64\r\n") &&
           shell_write_text("                     Memory usable: ") &&
           shell_write_u64(info.usable_memory_bytes) &&
           shell_write_text(" bytes\r\n                     Memory free: ") &&
           shell_write_u64(info.free_memory_bytes) &&
           shell_write_text(" bytes\r\n                     Root FS: BoringFS\r\n") &&
           shell_write_text("                     Shell: boring-shell\r\n");
}

static bool shell_execute_line(char *line) {
    char *command = NULL;
    char *argument = NULL;

    if (!shell_split(line, &command, &argument)) {
        return false;
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
    if (shell_text_equals(command, "cat")) {
        return shell_command_cat(argument);
    }
    if (shell_text_equals(command, "touch")) {
        return shell_command_touch(argument);
    }
    if (shell_text_equals(command, "write")) {
        return shell_command_write(argument);
    }
    if (shell_text_equals(command, "rm")) {
        return shell_command_rm(argument);
    }
    if (shell_text_equals(command, "boringfetch")) {
        return shell_command_boringfetch(argument);
    }

    return shell_write_text("command not found: ") &&
           shell_write_text(command) && shell_write_text("\r\n");
}

int boring_main(void) {
    static const char starting[] = "boring-shell: starting\r\n";
    static const char pid_ok[] = "boring-shell: pid 2\r\n";
    static const char ready[] = "boring-shell ready.\r\n\r\n";
    static const char failed[] = "boring-shell: FAILED\r\n";
    static const char prompt[] = "boring> ";
    char line[BORING_SHELL_LINE_MAX + 1U];

    shell_input_pushback_valid = false;
    shell_skip_lf_after_cr = false;
    if (!shell_write(starting, sizeof(starting) - 1U) ||
        (boring_getpid() != 2ULL) ||
        !shell_write(pid_ok, sizeof(pid_ok) - 1U) ||
        !shell_syscall_safety()) {
        (void)shell_write(failed, sizeof(failed) - 1U);
        shell_idle_forever();
    }

    shell_state = 1ULL;
    if ((shell_state != 1ULL) ||
        !shell_write(ready, sizeof(ready) - 1U)) {
        (void)shell_write(failed, sizeof(failed) - 1U);
        shell_idle_forever();
    }

    for (;;) {
        if (!shell_write_text(prompt) ||
            !shell_read_line(prompt, line, sizeof(line)) ||
            !shell_execute_line(line)) {
            (void)shell_write(failed, sizeof(failed) - 1U);
            shell_idle_forever();
        }
    }
}
