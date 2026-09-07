#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define CHILD_INPUT_CAPACITY 32U
#define PARENT_UNRELATED_FD_A 5U
#define PARENT_UNRELATED_FD_B 6U

int boring_main(int argc, char **argv);

static bool text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if ((left == NULL) || (right == NULL)) {
        return false;
    }
    while ((left[index] != '\0') && (right[index] != '\0')) {
        if (left[index] != right[index]) {
            return false;
        }
        ++index;
    }
    return left[index] == right[index];
}

static bool write_all(uint32_t fd, const char *text) {
    size_t offset = 0U;
    const size_t length = boring_strlen(text);
    while (offset < length) {
        const long result = boring_fd_write(fd, &text[offset], length - offset);
        if ((result <= 0L) || ((size_t)result > length - offset)) {
            return false;
        }
        offset += (size_t)result;
    }
    return true;
}

static bool read_line(char *buffer, size_t capacity) {
    size_t length = 0U;
    if ((buffer == NULL) || (capacity < 2U)) {
        return false;
    }
    while (length + 1U < capacity) {
        const long result = boring_fd_read(BORING_FD_STDIN, &buffer[length], 1U);
        if (result != 1L) {
            return false;
        }
        if (buffer[length] == '\n') {
            ++length;
            buffer[length] = '\0';
            return true;
        }
        ++length;
    }
    return false;
}

int boring_main(int argc, char **argv) {
    /* Force a real entry frame larger than one page, including with GCC
     * stack-clash probes; retain it across the blocking stdin schedule. */
    volatile uint8_t startup_stack[4096U + 128U];
    size_t stack_index;
    char input[CHILD_INPUT_CAPACITY];
    char probe = 0;

    for (stack_index = 0U; stack_index < sizeof(startup_stack); ++stack_index) {
        startup_stack[stack_index] = (uint8_t)(stack_index ^ 0x5aU);
    }
    if (!write_all(BORING_FD_STDOUT, "child: startup stack pages PASS\n")) {
        boring_exit(97);
    }

    if ((argc == 2) && text_equal(argv[0], "/bin/ipc-test") &&
        text_equal(argv[1], "detached")) {
        if (!write_all(BORING_FD_STDOUT, "child: detached stdout PASS\n") ||
            !write_all(BORING_FD_STDERR, "child: detached stderr PASS\n")) {
            boring_exit(91);
        }
        boring_exit(31);
    }

    if ((argc != 3) || !text_equal(argv[0], "/bin/ipc-test") ||
        !text_equal(argv[1], "foreground") ||
        !text_equal(argv[2], "argv-ok")) {
        boring_exit(90);
    }
    if (!write_all(BORING_FD_STDOUT, "child: argc-argv PASS\n") ||
        !write_all(BORING_FD_STDOUT, "child: stdout PASS\n") ||
        !write_all(BORING_FD_STDERR, "child: stderr PASS\n")) {
        boring_exit(92);
    }
    if ((boring_fd_read(PARENT_UNRELATED_FD_A, &probe, 1U) >= 0L) ||
        (boring_fd_read(PARENT_UNRELATED_FD_B, &probe, 1U) >= 0L)) {
        boring_exit(93);
    }
    if (!write_all(BORING_FD_STDOUT, "child: no-fd-inheritance PASS\n") ||
        !write_all(BORING_FD_STDOUT, "child: stdin-block-ready\n")) {
        boring_exit(94);
    }
    if (!read_line(input, sizeof(input)) || !text_equal(input, "wake-up\n")) {
        boring_exit(95);
    }
    for (stack_index = 0U; stack_index < sizeof(startup_stack); ++stack_index) {
        if (startup_stack[stack_index] != (uint8_t)(stack_index ^ 0x5aU)) {
            boring_exit(98);
        }
    }
    if (!write_all(BORING_FD_STDOUT, "child: stdin wake PASS\n") ||
        !write_all(BORING_FD_STDERR, "child: stderr-after-wake PASS\n") ||
        !write_all(BORING_FD_STDOUT, "child: exiting\n")) {
        boring_exit(96);
    }
    boring_exit(23);
}
