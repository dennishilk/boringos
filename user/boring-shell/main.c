#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/runtime.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define BORING_SHELL_LINE_MAX 512U
#define BORING_SHELL_ESCAPE_MAX 8U
#define BORING_SHELL_HISTORY_CAPACITY 16U
#define BORING_SHELL_COMMAND_NAME_CAPACITY 16U
#define BORING_SHELL_COMMAND_COUNT 18U
#define BORING_SHELL_PROMPT_CAPACITY \
    (BORING_SYSTEM_USERNAME_CAPACITY + BORING_SYSTEM_HOSTNAME_CAPACITY + \
     BORING_SYSCALL_CWD_MAX + 8U)
#define BORING_SHELL_MIB 1048576ULL

enum shell_completion_type {
    SHELL_COMPLETION_NONE = 0,
    SHELL_COMPLETION_ANY = 1,
    SHELL_COMPLETION_DIRECTORY = 2,
    SHELL_COMPLETION_REGULAR = 3
};

static volatile uint64_t shell_state;
static bool shell_skip_lf_after_cr;
static char shell_history[BORING_SHELL_HISTORY_CAPACITY]
                         [BORING_SHELL_LINE_MAX + 1U];
static char shell_history_draft[BORING_SHELL_LINE_MAX + 1U];
static size_t shell_history_start;
static size_t shell_history_count;
static uint64_t shell_history_total;
static char shell_prompt[BORING_SHELL_PROMPT_CAPACITY];
static char shell_completion_path[BORING_SHELL_LINE_MAX + 1U];
static char shell_completion_name[BORING_DIRENT_NAME_CAPACITY];
static char shell_write_buffer[BORING_SHELL_LINE_MAX + 1U];
static char shell_exec_path[BORING_SYSCALL_EXEC_PATH_MAX + 1U];
static const char shell_command_names[BORING_SHELL_COMMAND_COUNT]
                                     [BORING_SHELL_COMMAND_NAME_CAPACITY] = {
    "help", "ls", "mkdir", "rmdir", "cd", "touch", "write", "rm",
    "clear", "pwd", "echo", "hostname", "uname", "whoami",
    "ps", "history", "exit", "logout"
};

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
        result = boring_fd_write(BORING_FD_STDOUT, &buffer[offset], chunk);
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

static size_t shell_bounded_length(const char *text, size_t maximum) {
    size_t length;

    if (text == NULL) {
        return maximum + 1U;
    }
    for (length = 0U; length <= maximum; ++length) {
        if (text[length] == '\0') {
            return length;
        }
    }
    return maximum + 1U;
}

static bool shell_copy_text(char *destination,
                            size_t capacity,
                            const char *source) {
    size_t index;

    if ((destination == NULL) || (capacity == 0U) || (source == NULL)) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            return true;
        }
    }
    destination[capacity - 1U] = '\0';
    return false;
}

static bool shell_append_text(char *destination,
                              size_t capacity,
                              size_t *length,
                              const char *source) {
    size_t source_length;
    size_t index;

    if ((destination == NULL) || (capacity == 0U) || (length == NULL) ||
        (*length >= capacity) || (source == NULL)) {
        return false;
    }
    source_length = shell_bounded_length(source, capacity - 1U);
    if ((source_length >= capacity) ||
        (source_length > capacity - *length - 1U)) {
        return false;
    }
    for (index = 0U; index < source_length; ++index) {
        destination[*length + index] = source[index];
    }
    *length += source_length;
    destination[*length] = '\0';
    return true;
}

static bool shell_text_starts_with(const char *text,
                                   size_t text_length,
                                   const char *prefix,
                                   size_t prefix_length) {
    size_t index;

    if ((text == NULL) || (prefix == NULL) ||
        (prefix_length > text_length)) {
        return false;
    }
    for (index = 0U; index < prefix_length; ++index) {
        if (text[index] != prefix[index]) {
            return false;
        }
    }
    return true;
}

static bool shell_system_info(struct boring_system_info *info) {
    return (info != NULL) && (boring_system_info(info) == 0L) &&
           (info->abi_version == BORING_SYSTEM_INFO_ABI_VERSION) &&
           (shell_bounded_length(info->hostname,
                                 BORING_SYSTEM_HOSTNAME_CAPACITY - 1U) <
            BORING_SYSTEM_HOSTNAME_CAPACITY) &&
           (shell_bounded_length(info->username,
                                 BORING_SYSTEM_USERNAME_CAPACITY - 1U) <
            BORING_SYSTEM_USERNAME_CAPACITY) &&
           (shell_bounded_length(info->os_name,
                                 BORING_SYSTEM_OS_CAPACITY - 1U) <
            BORING_SYSTEM_OS_CAPACITY) &&
           (shell_bounded_length(info->kernel_name,
                                 BORING_SYSTEM_KERNEL_CAPACITY - 1U) <
            BORING_SYSTEM_KERNEL_CAPACITY) &&
           (shell_bounded_length(info->kernel_version,
                                 BORING_SYSTEM_VERSION_CAPACITY - 1U) <
            BORING_SYSTEM_VERSION_CAPACITY) &&
           (shell_bounded_length(info->arch,
                                 BORING_SYSTEM_ARCH_CAPACITY - 1U) <
            BORING_SYSTEM_ARCH_CAPACITY) &&
           (shell_bounded_length(info->root_fs,
                                 BORING_SYSTEM_FS_CAPACITY - 1U) <
            BORING_SYSTEM_FS_CAPACITY) &&
           (shell_bounded_length(info->root_device,
                                 BORING_SYSTEM_DEVICE_CAPACITY - 1U) <
            BORING_SYSTEM_DEVICE_CAPACITY);
}

static bool shell_build_prompt(void) {
    struct boring_system_info info;
    char cwd[BORING_SYSCALL_CWD_MAX + 1U];
    size_t length = 0U;
    const long cwd_length = boring_getcwd(cwd, sizeof(cwd));

    shell_prompt[0] = '\0';
    if ((cwd_length < 1L) || ((size_t)cwd_length >= sizeof(cwd)) ||
        (cwd[(size_t)cwd_length] != '\0') || !shell_system_info(&info)) {
        return false;
    }
    return shell_append_text(shell_prompt, sizeof(shell_prompt), &length,
                             info.username) &&
           shell_append_text(shell_prompt, sizeof(shell_prompt), &length,
                             "@") &&
           shell_append_text(shell_prompt, sizeof(shell_prompt), &length,
                             info.hostname) &&
           shell_append_text(shell_prompt, sizeof(shell_prompt), &length,
                             ":") &&
           shell_append_text(shell_prompt, sizeof(shell_prompt), &length,
                             cwd) &&
           shell_append_text(shell_prompt, sizeof(shell_prompt), &length,
                             "$ ");
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

static size_t shell_history_slot(size_t logical_index) {
    return (shell_history_start + logical_index) %
           (size_t)BORING_SHELL_HISTORY_CAPACITY;
}

static void shell_history_reset(void) {
    size_t slot;

    shell_history_start = 0U;
    shell_history_count = 0U;
    shell_history_total = 0ULL;
    shell_history_draft[0] = '\0';
    for (slot = 0U;
         slot < (size_t)BORING_SHELL_HISTORY_CAPACITY;
         ++slot) {
        shell_history[slot][0] = '\0';
    }
}

static bool shell_history_add(const char *line) {
    size_t slot;

    if (line == NULL) {
        return false;
    }
    if (line[0] == '\0') {
        return true;
    }
    if (shell_bounded_length(line, (size_t)BORING_SHELL_LINE_MAX) >
        (size_t)BORING_SHELL_LINE_MAX) {
        return false;
    }
    if (shell_history_count < (size_t)BORING_SHELL_HISTORY_CAPACITY) {
        slot = shell_history_slot(shell_history_count);
        ++shell_history_count;
    } else {
        shell_history_start =
            (shell_history_start + 1U) %
            (size_t)BORING_SHELL_HISTORY_CAPACITY;
        slot = shell_history_slot(shell_history_count - 1U);
    }
    if (!shell_copy_text(shell_history[slot], sizeof(shell_history[slot]),
                         line)) {
        return false;
    }
    ++shell_history_total;
    return true;
}

static bool shell_input_read(char *character_out) {
    long result;

    if (character_out == NULL) {
        return false;
    }
    result = boring_fd_read(BORING_FD_STDIN, character_out, 1U);
    return result == 1L;
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

static bool shell_bell(void) {
    return shell_write("\a", 1U);
}

static bool shell_replace_line(const char *prompt,
                               char *line,
                               size_t capacity,
                               size_t *length,
                               size_t *cursor,
                               const char *replacement) {
    const size_t replacement_length =
        shell_bounded_length(replacement, capacity - 1U);

    if ((prompt == NULL) || (line == NULL) || (capacity < 2U) ||
        (length == NULL) || (cursor == NULL) ||
        (replacement_length >= capacity)) {
        return false;
    }
    if (!shell_copy_text(line, capacity, replacement)) {
        return false;
    }
    *length = replacement_length;
    *cursor = replacement_length;
    return shell_redraw_line(prompt, line, *length, *cursor);
}

static bool shell_history_navigate(const char *prompt,
                                   char *line,
                                   size_t capacity,
                                   size_t *length,
                                   size_t *cursor,
                                   size_t *history_index,
                                   bool up) {
    const char *replacement;

    if ((prompt == NULL) || (line == NULL) || (capacity < 2U) ||
        (length == NULL) || (cursor == NULL) || (history_index == NULL) ||
        (*history_index > shell_history_count)) {
        return false;
    }
    if (shell_history_count == 0U) {
        return shell_bell();
    }
    if (up) {
        if (*history_index == shell_history_count) {
            if (!shell_copy_text(shell_history_draft,
                                 sizeof(shell_history_draft), line)) {
                return false;
            }
            *history_index = shell_history_count - 1U;
        } else if (*history_index != 0U) {
            --(*history_index);
        } else {
            return shell_bell();
        }
        replacement = shell_history[shell_history_slot(*history_index)];
    } else {
        if (*history_index == shell_history_count) {
            return shell_bell();
        }
        if (*history_index + 1U < shell_history_count) {
            ++(*history_index);
            replacement = shell_history[shell_history_slot(*history_index)];
        } else {
            *history_index = shell_history_count;
            replacement = shell_history_draft;
        }
    }
    if (shell_bounded_length(replacement, capacity - 1U) >= capacity) {
        return shell_bell();
    }
    return shell_replace_line(prompt, line, capacity, length, cursor,
                              replacement);
}

static bool shell_line_insert(char *line,
                              size_t capacity,
                              size_t *length,
                              size_t *cursor,
                              const char *text,
                              size_t text_length) {
    size_t index;

    if ((line == NULL) || (capacity < 2U) || (length == NULL) ||
        (cursor == NULL) || (text == NULL) || (*cursor > *length) ||
        (*length >= capacity) || (text_length > capacity - *length - 1U)) {
        return false;
    }
    for (index = *length + 1U; index > *cursor; --index) {
        line[index + text_length - 1U] = line[index - 1U];
    }
    for (index = 0U; index < text_length; ++index) {
        line[*cursor + index] = text[index];
    }
    *cursor += text_length;
    *length += text_length;
    return true;
}

static bool shell_span_equals(const char *text,
                              size_t text_length,
                              const char *expected) {
    const size_t expected_length =
        shell_bounded_length(expected, BORING_SHELL_COMMAND_NAME_CAPACITY - 1U);

    return (expected_length == text_length) &&
           shell_text_starts_with(text, text_length, expected,
                                  expected_length);
}

static enum shell_completion_type shell_path_completion_type(
    const char *line,
    size_t token_start) {
    size_t command_end = 0U;
    size_t argument_start;

    if ((line == NULL) || (token_start == 0U)) {
        return SHELL_COMPLETION_NONE;
    }
    while ((command_end < token_start) &&
           !shell_is_space(line[command_end])) {
        ++command_end;
    }
    argument_start = command_end;
    while ((argument_start < token_start) &&
           shell_is_space(line[argument_start])) {
        ++argument_start;
    }
    if ((command_end == 0U) || (argument_start != token_start)) {
        return SHELL_COMPLETION_NONE;
    }
    if (shell_span_equals(line, command_end, "cd") ||
        shell_span_equals(line, command_end, "rmdir")) {
        return SHELL_COMPLETION_DIRECTORY;
    }
    if (shell_span_equals(line, command_end, "cat") ||
        shell_span_equals(line, command_end, "write") ||
        shell_span_equals(line, command_end, "rm")) {
        return SHELL_COMPLETION_REGULAR;
    }
    if (shell_span_equals(line, command_end, "ls")) {
        return SHELL_COMPLETION_ANY;
    }
    return SHELL_COMPLETION_NONE;
}

static size_t shell_common_prefix(const char *left,
                                  size_t left_length,
                                  const char *right,
                                  size_t right_length) {
    size_t length = 0U;
    const size_t maximum =
        (left_length < right_length) ? left_length : right_length;

    while ((length < maximum) && (left[length] == right[length])) {
        ++length;
    }
    return length;
}

static bool shell_apply_completion(const char *prompt,
                                   char *line,
                                   size_t capacity,
                                   size_t *length,
                                   size_t *cursor,
                                   size_t typed_length,
                                   size_t completion_length,
                                   bool append_separator,
                                   char separator) {
    size_t target_length = completion_length;
    size_t insert_length;

    if ((prompt == NULL) || (line == NULL) || (length == NULL) ||
        (cursor == NULL) || (typed_length > completion_length) ||
        (completion_length >= sizeof(shell_completion_name))) {
        return false;
    }
    if (append_separator) {
        shell_completion_name[target_length] = separator;
        ++target_length;
    }
    insert_length = target_length - typed_length;
    if ((insert_length == 0U) ||
        !shell_line_insert(line, capacity, length, cursor,
                           &shell_completion_name[typed_length],
                           insert_length)) {
        return shell_bell();
    }
    return shell_redraw_line(prompt, line, *length, *cursor);
}

static bool shell_command_completion_consider(
    const char *candidate,
    size_t candidate_length,
    const char *prefix,
    size_t prefix_length,
    size_t *common_length,
    size_t *matches) {
    if ((candidate == NULL) || (prefix == NULL) ||
        (common_length == NULL) || (matches == NULL) ||
        (candidate_length == 0U) ||
        (candidate_length >= sizeof(shell_completion_name))) {
        return false;
    }
    if (!shell_text_starts_with(candidate, candidate_length,
                                prefix, prefix_length)) {
        return true;
    }
    if (*matches == 0U) {
        size_t index;

        for (index = 0U; index < candidate_length; ++index) {
            shell_completion_name[index] = candidate[index];
        }
        shell_completion_name[candidate_length] = '\0';
        *common_length = candidate_length;
    } else {
        *common_length = shell_common_prefix(
            shell_completion_name, *common_length,
            candidate, candidate_length);
    }
    ++(*matches);
    return true;
}

static bool shell_complete_command(const char *prompt,
                                   char *line,
                                   size_t capacity,
                                   size_t *length,
                                   size_t *cursor) {
    static const char bin_path[] = "/bin";
    const size_t prefix_length = *cursor;
    size_t common_length = 0U;
    size_t matches = 0U;
    size_t command_index;
    uint64_t directory_index = 0ULL;

    for (command_index = 0U;
         command_index < (size_t)BORING_SHELL_COMMAND_COUNT;
         ++command_index) {
        const size_t candidate_length = shell_bounded_length(
            shell_command_names[command_index],
            BORING_SHELL_COMMAND_NAME_CAPACITY - 1U);

        if (!shell_command_completion_consider(
                shell_command_names[command_index], candidate_length,
                line, prefix_length, &common_length, &matches)) {
            return false;
        }
    }
    for (;;) {
        struct boring_dirent entry;
        const long result = boring_fs_readdir(
            bin_path, sizeof(bin_path) - 1U,
            directory_index, &entry);

        if (result == 0L) {
            break;
        }
        if ((result == -(long)BORING_SYSCALL_ENOENT) ||
            (result == -(long)BORING_SYSCALL_ENOTDIR)) {
            break;
        }
        if (result < 0L) {
            return false;
        }
        if ((result != 1L) ||
            (entry.name_length == 0U) ||
            (entry.name_length >=
             (uint32_t)BORING_DIRENT_NAME_CAPACITY) ||
            (entry.name[entry.name_length] != '\0') ||
            ((entry.type != BORING_DIRENT_TYPE_DIRECTORY) &&
             (entry.type != BORING_DIRENT_TYPE_REGULAR))) {
            return false;
        }
        ++directory_index;
        if (entry.type != BORING_DIRENT_TYPE_REGULAR) {
            continue;
        }
        if (!shell_command_completion_consider(
                entry.name, (size_t)entry.name_length,
                line, prefix_length, &common_length, &matches)) {
            return false;
        }
    }
    if (matches == 0U) {
        return shell_bell();
    }
    return shell_apply_completion(
        prompt, line, capacity, length, cursor,
        prefix_length, common_length, matches == 1U, ' ');
}

static bool shell_complete_path(const char *prompt,
                                char *line,
                                size_t capacity,
                                size_t *length,
                                size_t *cursor,
                                size_t token_start,
                                enum shell_completion_type completion_type) {
    size_t last_slash = SIZE_MAX;
    size_t index;
    size_t prefix_start;
    size_t prefix_length;
    size_t directory_length;
    size_t common_length = 0U;
    size_t matches = 0U;
    uint32_t single_type = 0U;
    uint64_t directory_index = 0ULL;

    for (index = token_start; index < *cursor; ++index) {
        if (line[index] == '/') {
            last_slash = index;
        }
    }
    if (last_slash == SIZE_MAX) {
        shell_completion_path[0] = '.';
        shell_completion_path[1] = '\0';
        directory_length = 1U;
        prefix_start = token_start;
    } else {
        prefix_start = last_slash + 1U;
        if (last_slash == token_start) {
            shell_completion_path[0] = '/';
            shell_completion_path[1] = '\0';
            directory_length = 1U;
        } else {
            directory_length = last_slash - token_start;
            if (directory_length >= sizeof(shell_completion_path)) {
                return shell_bell();
            }
            for (index = 0U; index < directory_length; ++index) {
                shell_completion_path[index] = line[token_start + index];
            }
            shell_completion_path[directory_length] = '\0';
        }
    }
    prefix_length = *cursor - prefix_start;

    for (;;) {
        struct boring_dirent entry;
        const long result = boring_fs_readdir(
            shell_completion_path, directory_length, directory_index, &entry);
        size_t candidate_length;

        if (result == 0L) {
            break;
        }
        if (result < 0L) {
            return shell_bell();
        }
        if ((result != 1L) ||
            (entry.name_length >= (uint32_t)BORING_DIRENT_NAME_CAPACITY) ||
            (entry.name[entry.name_length] != '\0') ||
            ((entry.type != BORING_DIRENT_TYPE_DIRECTORY) &&
             (entry.type != BORING_DIRENT_TYPE_REGULAR))) {
            return false;
        }
        ++directory_index;
        if (((completion_type == SHELL_COMPLETION_DIRECTORY) &&
             (entry.type != BORING_DIRENT_TYPE_DIRECTORY)) ||
            ((completion_type == SHELL_COMPLETION_REGULAR) &&
             (entry.type != BORING_DIRENT_TYPE_REGULAR))) {
            continue;
        }
        candidate_length = (size_t)entry.name_length;
        if (!shell_text_starts_with(entry.name, candidate_length,
                                    &line[prefix_start], prefix_length)) {
            continue;
        }
        if (matches == 0U) {
            if (!shell_copy_text(shell_completion_name,
                                 sizeof(shell_completion_name), entry.name)) {
                return false;
            }
            common_length = candidate_length;
            single_type = entry.type;
        } else {
            common_length = shell_common_prefix(
                shell_completion_name, common_length,
                entry.name, candidate_length);
        }
        ++matches;
    }
    if (matches == 0U) {
        return shell_bell();
    }
    return shell_apply_completion(
        prompt, line, capacity, length, cursor, prefix_length, common_length,
        matches == 1U,
        (single_type == BORING_DIRENT_TYPE_DIRECTORY) ? '/' : ' ');
}

static bool shell_complete(const char *prompt,
                           char *line,
                           size_t capacity,
                           size_t *length,
                           size_t *cursor) {
    size_t token_start;
    enum shell_completion_type completion_type;

    if ((prompt == NULL) || (line == NULL) || (capacity < 2U) ||
        (length == NULL) || (cursor == NULL) || (*cursor > *length)) {
        return false;
    }
    token_start = *cursor;
    while ((token_start != 0U) && !shell_is_space(line[token_start - 1U])) {
        --token_start;
    }
    if (token_start == 0U) {
        return shell_complete_command(prompt, line, capacity, length, cursor);
    }
    completion_type = shell_path_completion_type(line, token_start);
    if (completion_type == SHELL_COMPLETION_NONE) {
        return shell_bell();
    }
    return shell_complete_path(prompt, line, capacity, length, cursor,
                               token_start, completion_type);
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
                                size_t capacity,
                                size_t *length,
                                size_t *cursor,
                                size_t *history_index) {
    char first;
    char sequence[BORING_SHELL_ESCAPE_MAX];
    size_t sequence_length = 0U;
    bool final_seen = false;

    if ((prompt == NULL) || (line == NULL) || (capacity < 2U) ||
        (length == NULL) || (cursor == NULL) || (history_index == NULL) ||
        (*cursor > *length) || !shell_input_read(&first)) {
        return false;
    }

    if (first == 'O') {
        char final;
        if (!shell_input_read(&final)) {
            return false;
        }
        if (final == 'H') {
            *cursor = 0U;
            return shell_redraw_line(prompt, line, *length, *cursor);
        }
        if (final == 'F') {
            *cursor = *length;
            return shell_redraw_line(prompt, line, *length, *cursor);
        }
        if (final == 'A') {
            return shell_history_navigate(prompt, line, capacity, length,
                                          cursor, history_index, true);
        }
        if (final == 'B') {
            return shell_history_navigate(prompt, line, capacity, length,
                                          cursor, history_index, false);
        }
        return true;
    }

    if (first != '[') {
        /* ESC-prefixed input is control input. Never put its suffix back into
         * the command buffer, even when this small editor does not implement
         * that particular terminal sequence.
         */
        return true;
    }

    while (!final_seen) {
        char character;
        const unsigned char byte_min = 0x40U;
        const unsigned char byte_max = 0x7eU;
        unsigned char byte;

        if (!shell_input_read(&character)) {
            return false;
        }
        byte = (unsigned char)character;
        if (sequence_length < (size_t)BORING_SHELL_ESCAPE_MAX) {
            sequence[sequence_length] = character;
            ++sequence_length;
        }
        if ((byte >= byte_min) && (byte <= byte_max)) {
            final_seen = true;
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
        if (*cursor < *length) {
            ++(*cursor);
            return shell_write("\x1b[C", 3U);
        }
        return true;
    }
    if (shell_escape_equals(sequence, sequence_length, "H") ||
        shell_escape_equals(sequence, sequence_length, "1~") ||
        shell_escape_equals(sequence, sequence_length, "7~")) {
        *cursor = 0U;
        return shell_redraw_line(prompt, line, *length, *cursor);
    }
    if (shell_escape_equals(sequence, sequence_length, "F") ||
        shell_escape_equals(sequence, sequence_length, "4~") ||
        shell_escape_equals(sequence, sequence_length, "8~")) {
        *cursor = *length;
        return shell_redraw_line(prompt, line, *length, *cursor);
    }
    if (shell_escape_equals(sequence, sequence_length, "A")) {
        return shell_history_navigate(prompt, line, capacity, length, cursor,
                                      history_index, true);
    }
    if (shell_escape_equals(sequence, sequence_length, "B")) {
        return shell_history_navigate(prompt, line, capacity, length, cursor,
                                      history_index, false);
    }
    if (shell_escape_equals(sequence, sequence_length, "3~")) {
        size_t index;

        if (*cursor >= *length) {
            return true;
        }
        for (index = *cursor; index < *length; ++index) {
            line[index] = line[index + 1U];
        }
        --(*length);
        return shell_redraw_line(prompt, line, *length, *cursor);
    }
    return true;
}

static bool shell_read_line(const char *prompt, char *line, size_t capacity) {
    size_t length = 0U;
    size_t cursor = 0U;
    size_t history_index = shell_history_count;
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
            if (!shell_handle_escape(prompt, line, capacity, &length, &cursor,
                                     &history_index)) {
                return false;
            }
            continue;
        }
        if (character == '\t') {
            if (!overflow &&
                !shell_complete(prompt, line, capacity, &length, &cursor)) {
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
    } else if (result == -(long)BORING_SYSCALL_EISDIR) {
        message = "is a directory";
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
    return shell_write_text("Filesystem:\r\n") &&
           shell_write_text("  ls cd pwd mkdir rmdir touch write rm\r\n") &&
           shell_write_text("\r\nShell:\r\n") &&
           shell_write_text("  clear echo history help exit logout\r\n") &&
           shell_write_text("\r\nSystem:\r\n") &&
           shell_write_text("  uname hostname whoami ps\r\n") &&
           shell_write_text("\r\nPrograms (/bin):\r\n") &&
           shell_write_text("  boringfetch cat\r\n");
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
    size_t index;
    bool newline = true;
    long result;

    if ((argument == NULL) || (argument[0] == '\0')) {
        return shell_write_text(
            "write: usage: write [-n] <path> <text>\r\n");
    }
    if ((argument[0] == '-') && (argument[1] == 'n') &&
        shell_is_space(argument[2])) {
        newline = false;
        argument = shell_trim(&argument[3]);
    }
    path = argument;
    cursor = path;
    while ((*cursor != '\0') && !shell_is_space(*cursor)) {
        ++cursor;
    }
    if (*cursor == '\0') {
        return shell_write_text(
            "write: usage: write [-n] <path> <text>\r\n");
    }
    *cursor = '\0';
    ++cursor;
    text = shell_trim(cursor);
    if ((text == NULL) || (text[0] == '\0')) {
        return shell_write_text(
            "write: usage: write [-n] <path> <text>\r\n");
    }
    text_length = boring_strlen(text);
    if (newline) {
        if (text_length >= sizeof(shell_write_buffer)) {
            return shell_print_fs_error(
                "write", -(long)BORING_SYSCALL_ENAMETOOLONG);
        }
        for (index = 0U; index < text_length; ++index) {
            shell_write_buffer[index] = text[index];
        }
        shell_write_buffer[text_length] = '\n';
        ++text_length;
        text = shell_write_buffer;
    }
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

static bool shell_command_clear(const char *argument) {
    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("clear: usage: clear\r\n");
    }
    return shell_write("\x1b[2J\x1b[H", 7U);
}

static bool shell_command_pwd(const char *argument) {
    char cwd[BORING_SYSCALL_CWD_MAX + 1U];
    long result;

    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("pwd: usage: pwd\r\n");
    }
    result = boring_getcwd(cwd, sizeof(cwd));
    if ((result < 1L) || ((size_t)result >= sizeof(cwd)) ||
        (cwd[(size_t)result] != '\0')) {
        return shell_write_text("pwd: current directory unavailable\r\n");
    }
    return shell_write(cwd, (size_t)result) && shell_write("\r\n", 2U);
}

static bool shell_command_echo(const char *argument) {
    return ((argument == NULL) || shell_write_text(argument)) &&
           shell_write("\r\n", 2U);
}

static bool shell_command_hostname(const char *argument) {
    struct boring_system_info info;

    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("hostname: usage: hostname\r\n");
    }
    if (!shell_system_info(&info)) {
        return shell_write_text("hostname: system information unavailable\r\n");
    }
    return shell_write_text(info.hostname) && shell_write("\r\n", 2U);
}

static bool shell_command_uname(const char *argument) {
    struct boring_system_info info;

    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("uname: usage: uname\r\n");
    }
    if (!shell_system_info(&info)) {
        return shell_write_text("uname: system information unavailable\r\n");
    }
    return shell_write_text(info.os_name) && shell_write_text(" ") &&
           shell_write_text(info.kernel_name) && shell_write_text(" ") &&
           shell_write_text(info.kernel_version) && shell_write_text(" ") &&
           shell_write_text(info.arch) && shell_write("\r\n", 2U);
}

static bool shell_command_whoami(const char *argument) {
    struct boring_system_info info;

    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("whoami: usage: whoami\r\n");
    }
    if (!shell_system_info(&info)) {
        return shell_write_text("whoami: process identity unavailable\r\n");
    }
    return shell_write_text(info.username) && shell_write("\r\n", 2U);
}

static const char *shell_process_state(uint32_t state) {
    if (state == BORING_PROCESS_STATE_RUNNING) {
        return "RUNNING";
    }
    if (state == BORING_PROCESS_STATE_WAITING) {
        return "WAITING";
    }
    if (state == BORING_PROCESS_STATE_ZOMBIE) {
        return "ZOMBIE";
    }
    return NULL;
}

static bool shell_command_ps(const char *argument) {
    uint64_t index = 0ULL;

    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("ps: usage: ps\r\n");
    }
    if (!shell_write_text("PID PPID STATE NAME\r\n")) {
        return false;
    }
    for (;;) {
        struct boring_process_info info;
        const long result = boring_process_snapshot(index, &info);
        const char *state;

        if (result == 0L) {
            return true;
        }
        if (result < 0L) {
            return shell_write_text("ps: process snapshot unavailable\r\n");
        }
        state = shell_process_state(info.state);
        if ((result != 1L) || (state == NULL) ||
            (shell_bounded_length(info.name,
                                  BORING_PROCESS_NAME_CAPACITY - 1U) >=
             BORING_PROCESS_NAME_CAPACITY)) {
            return false;
        }
        if (!shell_write_u64(info.pid) || !shell_write_text(" ") ||
            !shell_write_u64(info.parent_pid) || !shell_write_text(" ") ||
            !shell_write_text(state) || !shell_write_text(" ") ||
            !shell_write_text(info.name) || !shell_write("\r\n", 2U)) {
            return false;
        }
        ++index;
    }
}

static bool shell_command_history(const char *argument) {
    size_t index;
    uint64_t number = shell_history_total - (uint64_t)shell_history_count + 1ULL;

    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text("history: usage: history\r\n");
    }
    for (index = 0U; index < shell_history_count; ++index) {
        if (!shell_write_u64(number + (uint64_t)index) ||
            !shell_write_text("  ") ||
            !shell_write_text(shell_history[shell_history_slot(index)]) ||
            !shell_write("\r\n", 2U)) {
            return false;
        }
    }
    return true;
}

static bool shell_build_exec_path(const char *command) {
    static const char bin_prefix[] = "/bin/";
    size_t command_length;
    size_t index;
    bool explicit_path = false;

    if ((command == NULL) || (command[0] == '\0')) {
        return false;
    }
    command_length = boring_strlen(command);
    if (command_length > (size_t)BORING_SYSCALL_EXEC_PATH_MAX) {
        return false;
    }
    for (index = 0U; index < command_length; ++index) {
        if (command[index] == '/') {
            explicit_path = true;
            break;
        }
    }
    if (explicit_path) {
        return shell_copy_text(shell_exec_path, sizeof(shell_exec_path),
                               command);
    }
    if (command_length > sizeof(shell_exec_path) - sizeof(bin_prefix)) {
        return false;
    }
    for (index = 0U; index < sizeof(bin_prefix) - 1U; ++index) {
        shell_exec_path[index] = bin_prefix[index];
    }
    for (index = 0U; index < command_length; ++index) {
        shell_exec_path[sizeof(bin_prefix) - 1U + index] = command[index];
    }
    shell_exec_path[sizeof(bin_prefix) - 1U + command_length] = '\0';
    return true;
}

static bool shell_command_external(char *command, char *argument) {
    const char *argv[BORING_SYSCALL_ARG_MAX];
    size_t argc = 0U;
    char *cursor;
    const struct boring_spawn_stdio stdio_config = {
        BORING_FD_STDIN, BORING_FD_STDOUT, BORING_FD_STDERR, 0U
    };
    long launch_result;
    long wait_result;
    int status = 0;

    if ((command == NULL) || (command[0] == '\0') ||
        !shell_build_exec_path(command)) {
        return shell_write_text("command name too long\r\n");
    }
    argv[argc] = command;
    ++argc;
    cursor = argument;
    while ((cursor != NULL) && (cursor[0] != '\0')) {
        while ((cursor[0] != '\0') && shell_is_space(cursor[0])) {
            ++cursor;
        }
        if (cursor[0] == '\0') {
            break;
        }
        if (argc >= (size_t)BORING_SYSCALL_ARG_MAX) {
            return shell_write_text("shell: too many arguments\r\n");
        }
        argv[argc] = cursor;
        ++argc;
        while ((cursor[0] != '\0') && !shell_is_space(cursor[0])) {
            ++cursor;
        }
        if (cursor[0] != '\0') {
            cursor[0] = '\0';
            ++cursor;
        }
    }

    launch_result = boring_spawn(
        shell_exec_path, boring_strlen(shell_exec_path), argv, argc,
        &stdio_config);
    if (launch_result < 0L) {
        if (launch_result == -(long)BORING_SYSCALL_ENOENT) {
            return shell_write_text("command not found: ") &&
                   shell_write_text(command) && shell_write_text("\r\n");
        }
        if (launch_result == -(long)BORING_SYSCALL_ENOEXEC) {
            return shell_write_text(command) &&
                   shell_write_text(": cannot execute\r\n");
        }
        if (launch_result == -(long)BORING_SYSCALL_EISDIR) {
            return shell_write_text(command) &&
                   shell_write_text(": is a directory\r\n");
        }
        if (launch_result == -(long)BORING_SYSCALL_ENAMETOOLONG) {
            return shell_write_text(command) &&
                   shell_write_text(": name too long\r\n");
        }
        return shell_write_text(command) &&
               shell_write_text(": launch failed\r\n");
    }
    if (launch_result == 0L) {
        return false;
    }
    wait_result = boring_waitpid((uint64_t)launch_result, &status);
    if (wait_result != launch_result) {
        return shell_write_text(command) &&
               shell_write_text(": wait failed\r\n");
    }
    return true;
}

static bool shell_command_exit(const char *command, const char *argument) {
    if ((argument != NULL) && (argument[0] != '\0')) {
        return shell_write_text(command) &&
               shell_write_text(": usage: ") &&
               shell_write_text(command) && shell_write("\r\n", 2U);
    }
    boring_exit(0);
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
    if (shell_text_equals(command, "touch")) {
        return shell_command_touch(argument);
    }
    if (shell_text_equals(command, "write")) {
        return shell_command_write(argument);
    }
    if (shell_text_equals(command, "rm")) {
        return shell_command_rm(argument);
    }
    if (shell_text_equals(command, "clear")) {
        return shell_command_clear(argument);
    }
    if (shell_text_equals(command, "pwd")) {
        return shell_command_pwd(argument);
    }
    if (shell_text_equals(command, "echo")) {
        return shell_command_echo(argument);
    }
    if (shell_text_equals(command, "hostname")) {
        return shell_command_hostname(argument);
    }
    if (shell_text_equals(command, "uname")) {
        return shell_command_uname(argument);
    }
    if (shell_text_equals(command, "whoami")) {
        return shell_command_whoami(argument);
    }
    if (shell_text_equals(command, "ps")) {
        return shell_command_ps(argument);
    }
    if (shell_text_equals(command, "history")) {
        return shell_command_history(argument);
    }
    if (shell_text_equals(command, "exit")) {
        return shell_command_exit("exit", argument);
    }
    if (shell_text_equals(command, "logout")) {
        return shell_command_exit("logout", argument);
    }
    return shell_command_external(command, argument);
}

int boring_main(void) {
    static const char starting[] = "boring-shell: starting\r\n";
    static const char pid_prefix[] = "boring-shell: pid ";
    static const char ready[] = "boring-shell ready.\r\n\r\n";
    static const char failed[] = "boring-shell: FAILED\r\n";
    char line[BORING_SHELL_LINE_MAX + 1U];
    const uint64_t pid = boring_getpid();

    shell_skip_lf_after_cr = false;
    shell_history_reset();
    if (!shell_write(starting, sizeof(starting) - 1U) ||
        (pid < 2ULL) ||
        !shell_write(pid_prefix, sizeof(pid_prefix) - 1U) ||
        !shell_write_u64(pid) || !shell_write("\r\n", 2U) ||
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
        if (!shell_build_prompt() || !shell_write_text(shell_prompt) ||
            !shell_read_line(shell_prompt, line, sizeof(line)) ||
            !shell_history_add(line) ||
            !shell_execute_line(line)) {
            (void)shell_write(failed, sizeof(failed) - 1U);
            shell_idle_forever();
        }
    }
}
