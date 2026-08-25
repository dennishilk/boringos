#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define BORINGFETCH_MIB 1048576ULL

int boring_main(int argc, char **argv);

static bool boringfetch_write(const char *buffer, size_t length) {
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

static bool boringfetch_text(const char *text) {
    return (text != NULL) && boringfetch_write(text, boring_strlen(text));
}

static bool boringfetch_u64(uint64_t value) {
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
    return boringfetch_write(digits, count);
}

static size_t boringfetch_bounded_length(const char *text, size_t maximum) {
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

static bool boringfetch_info(struct boring_system_info *info) {
    return (info != NULL) && (boring_system_info(info) == 0L) &&
           (info->abi_version == BORING_SYSTEM_INFO_ABI_VERSION) &&
           (boringfetch_bounded_length(info->hostname,
                                       BORING_SYSTEM_HOSTNAME_CAPACITY - 1U) <
            BORING_SYSTEM_HOSTNAME_CAPACITY) &&
           (boringfetch_bounded_length(info->username,
                                       BORING_SYSTEM_USERNAME_CAPACITY - 1U) <
            BORING_SYSTEM_USERNAME_CAPACITY) &&
           (boringfetch_bounded_length(info->os_name,
                                       BORING_SYSTEM_OS_CAPACITY - 1U) <
            BORING_SYSTEM_OS_CAPACITY) &&
           (boringfetch_bounded_length(info->kernel_name,
                                       BORING_SYSTEM_KERNEL_CAPACITY - 1U) <
            BORING_SYSTEM_KERNEL_CAPACITY) &&
           (boringfetch_bounded_length(info->kernel_version,
                                       BORING_SYSTEM_VERSION_CAPACITY - 1U) <
            BORING_SYSTEM_VERSION_CAPACITY) &&
           (boringfetch_bounded_length(info->arch,
                                       BORING_SYSTEM_ARCH_CAPACITY - 1U) <
            BORING_SYSTEM_ARCH_CAPACITY) &&
           (boringfetch_bounded_length(info->root_fs,
                                       BORING_SYSTEM_FS_CAPACITY - 1U) <
            BORING_SYSTEM_FS_CAPACITY) &&
           (boringfetch_bounded_length(info->root_device,
                                       BORING_SYSTEM_DEVICE_CAPACITY - 1U) <
            BORING_SYSTEM_DEVICE_CAPACITY);
}

static bool boringfetch_render(void) {
    struct boring_system_info info;
    uint64_t used_memory;
    uint64_t uptime_seconds = 0ULL;
    bool uptime_available = false;

    if (!boringfetch_info(&info) ||
        (info.free_memory_bytes > info.usable_memory_bytes)) {
        return false;
    }
    used_memory = info.usable_memory_bytes - info.free_memory_bytes;
    if (info.timer_frequency_millihz != 0U) {
        const uint64_t frequency = (uint64_t)info.timer_frequency_millihz;
        const uint64_t whole = info.uptime_ticks / frequency;

        if (whole <= UINT64_MAX / 1000ULL) {
            uptime_seconds = whole * 1000ULL +
                ((info.uptime_ticks % frequency) * 1000ULL) / frequency;
            uptime_available = true;
        }
    }
    if (!boringfetch_text("    ____             BoringOS\r\n") ||
        !boringfetch_text("   / __ )____  _____ -------------------------\r\n") ||
        !boringfetch_text("  / __  / __ \\/ ___/ OS: ") ||
        !boringfetch_text(info.os_name) || !boringfetch_text("\r\n") ||
        !boringfetch_text(" / /_/ / /_/ / /    Kernel: ") ||
        !boringfetch_text(info.kernel_name) || !boringfetch_text(" ") ||
        !boringfetch_text(info.kernel_version) || !boringfetch_text("\r\n") ||
        !boringfetch_text("/_____/\\____/_/      Arch: ") ||
        !boringfetch_text(info.arch) || !boringfetch_text("\r\n") ||
        !boringfetch_text("                     Hostname: ") ||
        !boringfetch_text(info.hostname) || !boringfetch_text("\r\n") ||
        !boringfetch_text("                     User: ") ||
        !boringfetch_text(info.username) || !boringfetch_text("\r\n") ||
        !boringfetch_text("                     Shell: boring-shell\r\n") ||
        !boringfetch_text("                     Root FS: ") ||
        !boringfetch_text(info.root_fs) || !boringfetch_text("\r\n") ||
        !boringfetch_text("                     Root device: ") ||
        !boringfetch_text(info.root_device) || !boringfetch_text("\r\n") ||
        !boringfetch_text("                     Memory: ") ||
        !boringfetch_u64(used_memory / BORINGFETCH_MIB) ||
        !boringfetch_text(" MiB / ") ||
        !boringfetch_u64(info.usable_memory_bytes / BORINGFETCH_MIB) ||
        !boringfetch_text(" MiB\r\n") ||
        !boringfetch_text("                     Free memory: ") ||
        !boringfetch_u64(info.free_memory_bytes / BORINGFETCH_MIB) ||
        !boringfetch_text(" MiB\r\n") ||
        !boringfetch_text("                     Processes: ") ||
        !boringfetch_u64((uint64_t)info.process_count) ||
        !boringfetch_text("\r\n                     PID: ") ||
        !boringfetch_u64(info.current_pid) || !boringfetch_text("\r\n")) {
        return false;
    }
    if (!uptime_available) {
        return true;
    }
    return boringfetch_text("                     Uptime: ") &&
           boringfetch_u64(uptime_seconds) && boringfetch_text(" s\r\n");
}

int boring_main(int argc, char **argv) {
    int status = 0;

    if ((argc != 1) || (argv == NULL) || (argv[0] == NULL)) {
        (void)boringfetch_text("boringfetch: no arguments supported\r\n");
        status = 2;
    } else if (!boringfetch_render()) {
        (void)boringfetch_text("boringfetch: system information unavailable\r\n");
        status = 1;
    }
    boring_exit(status);
}
