#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define BORINGFETCH_MIB 1048576ULL
#define BORINGFETCH_DATA_MARKER 0x424f52494e474654ULL

static volatile uint64_t boringfetch_data_marker =
    BORINGFETCH_DATA_MARKER;

int boring_main(int argc, char **argv);

static bool boringfetch_write(const char *buffer, size_t length) {
    size_t offset = 0U;

    if ((buffer == NULL) && (length != 0U)) {
        return false;
    }
    while (offset < length) {
        size_t chunk = length - offset;
        long result;

        if (chunk > (size_t)BORING_SYSCALL_FD_IO_MAX) {
            chunk = (size_t)BORING_SYSCALL_FD_IO_MAX;
        }
        result = boring_fd_write(BORING_FD_STDOUT, &buffer[offset], chunk);
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

static bool boringfetch_hex(uint64_t value, size_t digits) {
    static const char hexadecimal[] = "0123456789ABCDEF";
    char text[16];
    size_t index;

    if ((digits == 0U) || (digits > sizeof(text))) {
        return false;
    }
    for (index = 0U; index < digits; ++index) {
        const size_t shift = (digits - index - 1U) * 4U;
        text[index] = hexadecimal[(value >> shift) & 0xfULL];
    }
    return boringfetch_write(text, digits);
}

static bool boringfetch_size(uint64_t bytes) {
    if (bytes >= BORINGFETCH_MIB) {
        return boringfetch_u64(bytes / BORINGFETCH_MIB) &&
               boringfetch_text(" MiB");
    }
    if (bytes >= 1024ULL) {
        return boringfetch_u64(bytes / 1024ULL) &&
               boringfetch_text(" KiB");
    }
    return boringfetch_u64(bytes) && boringfetch_text(" bytes");
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

static bool boringfetch_field_valid(const char *text, size_t capacity) {
    return (capacity != 0U) &&
           (boringfetch_bounded_length(text, capacity - 1U) < capacity);
}

static bool boringfetch_info(struct boring_system_info *info) {
    size_t index;

    if ((info == NULL) || (boring_system_info(info) != 0L) ||
        (info->abi_version != BORING_SYSTEM_INFO_ABI_VERSION) ||
        (info->pci_sample_count > BORING_SYSTEM_PCI_SAMPLE_MAX)) {
        return false;
    }
    for (index = 0U; index < sizeof(info->reserved_hardware); ++index) {
        if (info->reserved_hardware[index] != 0U) {
            return false;
        }
    }
    return boringfetch_field_valid(info->hostname, sizeof(info->hostname)) &&
           boringfetch_field_valid(info->username, sizeof(info->username)) &&
           boringfetch_field_valid(info->os_name, sizeof(info->os_name)) &&
           boringfetch_field_valid(info->kernel_name,
                                   sizeof(info->kernel_name)) &&
           boringfetch_field_valid(info->kernel_version,
                                   sizeof(info->kernel_version)) &&
           boringfetch_field_valid(info->arch, sizeof(info->arch)) &&
           boringfetch_field_valid(info->root_fs, sizeof(info->root_fs)) &&
           boringfetch_field_valid(info->root_device,
                                   sizeof(info->root_device)) &&
           boringfetch_field_valid(info->cpu_vendor,
                                   sizeof(info->cpu_vendor)) &&
           boringfetch_field_valid(info->cpu_brand,
                                   sizeof(info->cpu_brand)) &&
           boringfetch_field_valid(info->system_manufacturer,
                                   sizeof(info->system_manufacturer)) &&
           boringfetch_field_valid(info->system_product,
                                   sizeof(info->system_product)) &&
           boringfetch_field_valid(info->board_manufacturer,
                                   sizeof(info->board_manufacturer)) &&
           boringfetch_field_valid(info->board_product,
                                   sizeof(info->board_product)) &&
           boringfetch_field_valid(info->firmware_vendor,
                                   sizeof(info->firmware_vendor)) &&
           boringfetch_field_valid(info->firmware_version,
                                   sizeof(info->firmware_version)) &&
           boringfetch_field_valid(info->storage_name,
                                   sizeof(info->storage_name));
}

static bool boringfetch_pair(const char *label, const char *left,
                             const char *right) {
    if (!boringfetch_text("                     ") ||
        !boringfetch_text(label) || !boringfetch_text(": ")) {
        return false;
    }
    if ((left != NULL) && (left[0] != '\0') && !boringfetch_text(left)) {
        return false;
    }
    if ((left != NULL) && (left[0] != '\0') &&
        (right != NULL) && (right[0] != '\0') &&
        !boringfetch_text(" ")) {
        return false;
    }
    if ((right != NULL) && (right[0] != '\0') && !boringfetch_text(right)) {
        return false;
    }
    return boringfetch_text("\r\n");
}

static bool boringfetch_hardware(const struct boring_system_info *info) {
    size_t index;

    if ((info->hardware_flags & BORING_SYSTEM_HW_CPU) != 0ULL) {
        if (!boringfetch_pair("CPU", info->cpu_vendor, info->cpu_brand) ||
            !boringfetch_text("                     CPU ID: family ") ||
            !boringfetch_u64(info->cpu_family) ||
            !boringfetch_text(" model ") || !boringfetch_u64(info->cpu_model) ||
            !boringfetch_text(" stepping ") ||
            !boringfetch_u64(info->cpu_stepping) ||
            !boringfetch_text("\r\n")) {
            return false;
        }
    }
    if (((info->hardware_flags & BORING_SYSTEM_HW_SYSTEM) != 0ULL) &&
        !boringfetch_pair("Machine", info->system_manufacturer,
                          info->system_product)) {
        return false;
    }
    if (((info->hardware_flags & BORING_SYSTEM_HW_BOARD) != 0ULL) &&
        !boringfetch_pair("Board", info->board_manufacturer,
                          info->board_product)) {
        return false;
    }
    if (((info->hardware_flags & BORING_SYSTEM_HW_FIRMWARE) != 0ULL) &&
        !boringfetch_pair("Firmware", info->firmware_vendor,
                          info->firmware_version)) {
        return false;
    }
    if ((info->hardware_flags & BORING_SYSTEM_HW_PCI) != 0ULL) {
        if (!boringfetch_text("                     PCI: ") ||
            !boringfetch_u64(info->pci_device_count) ||
            !boringfetch_text(" devices\r\n")) {
            return false;
        }
        for (index = 0U; index < (size_t)info->pci_sample_count; ++index) {
            const struct boring_system_pci_sample *sample =
                &info->pci_samples[index];

            if (!boringfetch_text("                     PCI ") ||
                !boringfetch_hex(sample->bus, 2U) ||
                !boringfetch_text(":") ||
                !boringfetch_hex(sample->device, 2U) ||
                !boringfetch_text(".") ||
                !boringfetch_hex(sample->function, 1U) ||
                !boringfetch_text(" ") ||
                !boringfetch_hex(sample->vendor_id, 4U) ||
                !boringfetch_text(":") ||
                !boringfetch_hex(sample->device_id, 4U) ||
                !boringfetch_text(" class ") ||
                !boringfetch_hex(sample->class_code, 2U) ||
                !boringfetch_text(":") ||
                !boringfetch_hex(sample->subclass, 2U) ||
                !boringfetch_text(":") ||
                !boringfetch_hex(sample->prog_if, 2U) ||
                !boringfetch_text("\r\n")) {
                return false;
            }
        }
    }
    if ((info->hardware_flags & BORING_SYSTEM_HW_SMBIOS_MEMORY) != 0ULL) {
        if (!boringfetch_text("                     SMBIOS Memory: ") ||
            !boringfetch_u64(info->smbios_memory_bytes / BORINGFETCH_MIB) ||
            !boringfetch_text(" MiB (") ||
            !boringfetch_u64(info->smbios_memory_devices_present) ||
            !boringfetch_text("/") ||
            !boringfetch_u64(info->smbios_memory_slots) ||
            !boringfetch_text(" devices)\r\n")) {
            return false;
        }
    }
    if ((info->hardware_flags & BORING_SYSTEM_HW_FRAMEBUFFER) != 0ULL) {
        if (!boringfetch_text("                     Display: ") ||
            !boringfetch_u64(info->framebuffer_width) ||
            !boringfetch_text("x") ||
            !boringfetch_u64(info->framebuffer_height) ||
            !boringfetch_text("x") ||
            !boringfetch_u64(info->framebuffer_bpp) ||
            !boringfetch_text(" pitch ") ||
            !boringfetch_u64(info->framebuffer_pitch) ||
            !boringfetch_text("\r\n")) {
            return false;
        }
    }
    if ((info->hardware_flags & BORING_SYSTEM_HW_STORAGE) != 0ULL) {
        if (!boringfetch_text("                     Storage: ") ||
            !boringfetch_text(info->storage_name) ||
            !boringfetch_text(" ") ||
            !boringfetch_size(info->storage_bytes)) {
            return false;
        }
        if ((info->hardware_flags & BORING_SYSTEM_HW_STORAGE_PCI) != 0ULL) {
            if (!boringfetch_text(" ") ||
                !boringfetch_hex(info->storage_pci_vendor_id, 4U) ||
                !boringfetch_text(":") ||
                !boringfetch_hex(info->storage_pci_device_id, 4U)) {
                return false;
            }
        }
        if (!boringfetch_text("\r\n")) {
            return false;
        }
    }
    return true;
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
    if (uptime_available &&
        (!boringfetch_text("                     Uptime: ") ||
         !boringfetch_u64(uptime_seconds) ||
         !boringfetch_text(" s\r\n"))) {
        return false;
    }
    return boringfetch_hardware(&info);
}

int boring_main(int argc, char **argv) {
    int status = 0;

    if (boringfetch_data_marker != BORINGFETCH_DATA_MARKER) {
        (void)boringfetch_text("boringfetch: runtime data unavailable\r\n");
        status = 3;
    } else if ((argc != 1) || (argv == NULL) || (argv[0] == NULL)) {
        (void)boringfetch_text("boringfetch: no arguments supported\r\n");
        status = 2;
    } else if (!boringfetch_render()) {
        (void)boringfetch_text("boringfetch: system information unavailable\r\n");
        status = 1;
    }
    boring_exit(status);
}
