#include <boring/boot_protocol.h>
#include <boring/elf_boot.h>

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_module_request limine_module_request = {
    .id = BORING_LIMINE_MODULE_REQUEST_ID,
    .revision = 0ULL,
    .response = 0,
    .internal_module_count = 0ULL,
    .internal_modules = 0
};

const struct boring_limine_module_response *elf_boot_module_response(void) {
    return limine_module_request.response;
}
