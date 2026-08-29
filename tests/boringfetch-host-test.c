#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <boring/syscall.h>

#define OUTPUT_CAPACITY 65536U

static struct boring_system_info mock_info;
static char output[OUTPUT_CAPACITY];
static size_t output_length;
static int exit_status;
static jmp_buf exit_jump;

int boring_main(int argc, char **argv);

static void prepare_base(void) {
    (void)memset(&mock_info, 0, sizeof(mock_info));
    output_length = 0U;
    mock_info.abi_version = BORING_SYSTEM_INFO_ABI_VERSION;
    mock_info.usable_memory_bytes = 128ULL * 1024ULL * 1024ULL;
    mock_info.free_memory_bytes = 120ULL * 1024ULL * 1024ULL;
    mock_info.process_count = 3U;
    mock_info.current_pid = 7ULL;
    (void)strcpy(mock_info.hostname, "boringos");
    (void)strcpy(mock_info.username, "boring");
    (void)strcpy(mock_info.os_name, "BoringOS");
    (void)strcpy(mock_info.kernel_name, "BoringKernel");
    (void)strcpy(mock_info.kernel_version, "0.0.53-dev");
    (void)strcpy(mock_info.arch, "x86_64");
    (void)strcpy(mock_info.root_fs, "BoringFS");
    (void)strcpy(mock_info.root_device, "virtio-blk");
}

static int run_fetch(void) {
    char name[] = "boringfetch";
    char *arguments[] = {name, NULL};

    if (setjmp(exit_jump) == 0) {
        (void)boring_main(1, arguments);
        assert(false);
    }
    assert(output_length < sizeof(output));
    output[output_length] = '\0';
    return exit_status;
}

long boring_fd_write(uint32_t fd, const void *buffer, size_t length) {
    if ((fd != BORING_FD_STDOUT) || (buffer == NULL) ||
        (length > sizeof(output) - output_length - 1U)) {
        return -(long)BORING_SYSCALL_EIO;
    }
    (void)memcpy(&output[output_length], buffer, length);
    output_length += length;
    return (long)length;
}

long boring_system_info(struct boring_system_info *info) {
    if (info == NULL) {
        return -(long)BORING_SYSCALL_EFAULT;
    }
    *info = mock_info;
    return 0L;
}

void boring_exit(int status) {
    exit_status = status;
    longjmp(exit_jump, 1);
}

static void test_hardware_render(void) {
    struct boring_system_pci_sample *pci;

    prepare_base();
    mock_info.hardware_flags = BORING_SYSTEM_HW_CPU |
        BORING_SYSTEM_HW_SYSTEM | BORING_SYSTEM_HW_BOARD |
        BORING_SYSTEM_HW_FIRMWARE | BORING_SYSTEM_HW_SMBIOS_MEMORY |
        BORING_SYSTEM_HW_SMBIOS_MEMORY_COMPLETE | BORING_SYSTEM_HW_PCI |
        BORING_SYSTEM_HW_PCI_COMPLETE | BORING_SYSTEM_HW_FRAMEBUFFER |
        BORING_SYSTEM_HW_STORAGE | BORING_SYSTEM_HW_STORAGE_PCI;
    (void)strcpy(mock_info.cpu_vendor, "GenuineIntel");
    (void)strcpy(mock_info.cpu_brand, "Boring Test CPU");
    mock_info.cpu_family = 6U;
    mock_info.cpu_model = 158U;
    mock_info.cpu_stepping = 10U;
    (void)strcpy(mock_info.system_manufacturer, "Boring Systems");
    (void)strcpy(mock_info.system_product, "Model 46");
    (void)strcpy(mock_info.board_manufacturer, "Boring Boards");
    (void)strcpy(mock_info.board_product, "Board 46");
    (void)strcpy(mock_info.firmware_vendor, "Boring Firmware");
    (void)strcpy(mock_info.firmware_version, "46.0");
    mock_info.pci_device_count = 12U;
    mock_info.pci_sample_count = 1U;
    pci = &mock_info.pci_samples[0];
    pci->device = 5U;
    pci->vendor_id = 0x1af4U;
    pci->device_id = 0x1042U;
    pci->class_code = 0x01U;
    mock_info.smbios_memory_bytes = 128ULL * 1024ULL * 1024ULL;
    mock_info.smbios_memory_slots = 2U;
    mock_info.smbios_memory_devices_present = 1U;
    mock_info.framebuffer_width = 1280U;
    mock_info.framebuffer_height = 800U;
    mock_info.framebuffer_bpp = 32U;
    mock_info.framebuffer_pitch = 5120U;
    (void)strcpy(mock_info.storage_name, "vblk0");
    mock_info.storage_bytes = 64ULL * 1024ULL * 1024ULL;
    mock_info.storage_pci_vendor_id = 0x1af4U;
    mock_info.storage_pci_device_id = 0x1042U;

    assert(run_fetch() == 0);
    assert(strstr(output, "CPU: GenuineIntel Boring Test CPU\r\n") != NULL);
    assert(strstr(output, "CPU ID: family 6 model 158 stepping 10\r\n") != NULL);
    assert(strstr(output, "Machine: Boring Systems Model 46\r\n") != NULL);
    assert(strstr(output, "Board: Boring Boards Board 46\r\n") != NULL);
    assert(strstr(output, "Firmware: Boring Firmware 46.0\r\n") != NULL);
    assert(strstr(output, "PCI: 12 devices\r\n") != NULL);
    assert(strstr(output, "PCI 00:05.0 1AF4:1042 class 01:00:00\r\n") != NULL);
    assert(strstr(output, "SMBIOS Memory: 128 MiB (1/2 devices)\r\n") != NULL);
    assert(strstr(output, "Display: 1280x800x32 pitch 5120\r\n") != NULL);
    assert(strstr(output, "Storage: vblk0 64 MiB 1AF4:1042\r\n") != NULL);
}

static void test_unavailable_is_omitted(void) {
    prepare_base();
    assert(run_fetch() == 0);
    assert(strstr(output, "CPU:") == NULL);
    assert(strstr(output, "Machine:") == NULL);
    assert(strstr(output, "PCI:") == NULL);
    assert(strstr(output, "Display:") == NULL);
    assert(strstr(output, "Storage:") == NULL);
}

static void test_bounds_rejected(void) {
    prepare_base();
    mock_info.pci_sample_count = BORING_SYSTEM_PCI_SAMPLE_MAX + 1U;
    assert(run_fetch() == 1);
    assert(strstr(output, "system information unavailable") != NULL);

    prepare_base();
    (void)memset(mock_info.cpu_brand, 'X', sizeof(mock_info.cpu_brand));
    assert(run_fetch() == 1);
}

int main(void) {
    test_hardware_render();
    test_unavailable_is_omitted();
    test_bounds_rejected();
    (void)puts("boringfetch host tests passed");
    return 0;
}
