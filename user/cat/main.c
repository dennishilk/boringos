#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define CAT_BUFFER_SIZE 256U
#define CAT_DATA_MARKER 0x424f524341544644ULL

static volatile uint64_t cat_data_marker = CAT_DATA_MARKER;

int boring_main(int argc, char **argv);

static bool cat_write_all(uint32_t fd, const char *buffer, size_t length) {
    size_t offset = 0U;

    if ((buffer == NULL) && (length != 0U)) {
        return false;
    }
    while (offset < length) {
        const long result = boring_fd_write(fd, &buffer[offset], length - offset);

        if ((result <= 0L) || ((size_t)result > length - offset)) {
            return false;
        }
        offset += (size_t)result;
    }
    return true;
}

static bool cat_write_text(uint32_t fd, const char *text) {
    return (text != NULL) && cat_write_all(fd, text, boring_strlen(text));
}

static int cat_usage(void) {
    (void)cat_write_text(BORING_FD_STDERR, "cat: usage: cat <path>\r\n");
    return 2;
}

static int cat_file(const char *path) {
    char buffer[CAT_BUFFER_SIZE];
    const size_t path_length = boring_strlen(path);
    const long opened = boring_fd_open(path, path_length, BORING_FD_OPEN_READ);
    uint32_t fd;

    if (opened < 0L) {
        (void)cat_write_text(BORING_FD_STDERR, "cat: cannot open\r\n");
        return 1;
    }
    fd = (uint32_t)opened;
    for (;;) {
        const long result = boring_fd_read(fd, buffer, sizeof(buffer));

        if (result < 0L) {
            (void)boring_fd_close(fd);
            (void)cat_write_text(BORING_FD_STDERR, "cat: read failed\r\n");
            return 1;
        }
        if (result == 0L) {
            if (boring_fd_close(fd) != 0L) {
                (void)cat_write_text(BORING_FD_STDERR, "cat: close failed\r\n");
                return 1;
            }
            return 0;
        }
        if (((size_t)result > sizeof(buffer)) ||
            !cat_write_all(BORING_FD_STDOUT, buffer, (size_t)result)) {
            (void)boring_fd_close(fd);
            (void)cat_write_text(BORING_FD_STDERR, "cat: write failed\r\n");
            return 1;
        }
    }
}

int boring_main(int argc, char **argv) {
    int status;

    if (cat_data_marker != CAT_DATA_MARKER) {
        (void)cat_write_text(BORING_FD_STDERR, "cat: runtime data unavailable\r\n");
        boring_exit(3);
    }
    if ((argc != 2) || (argv == NULL) || (argv[0] == NULL) ||
        (argv[1] == NULL) || (argv[1][0] == '\0')) {
        boring_exit(cat_usage());
    }
    status = cat_file(argv[1]);
    boring_exit(status);
}
