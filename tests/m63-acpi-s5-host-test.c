#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/acpi_s5.h>

static void require(bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "m63 ACPI _S5 host test failed: %s\n", message);
        __builtin_trap();
    }
}

int main(void) {
    static const uint8_t standard[] = {
        0x08U, 0x5cU, '_', 'S', '5', '_',
        0x12U, 0x06U, 0x02U, 0x0aU, 0x05U, 0x0aU, 0x05U
    };
    static const uint8_t constants[] = {
        0x08U, '_', 'S', '5', '_',
        0x12U, 0x04U, 0x02U, 0x00U, 0x01U
    };
    static const uint8_t malformed[] = {
        0x08U, '_', 'S', '5', '_',
        0x12U, 0x3fU, 0x02U, 0x0aU, 0x05U
    };
    static const uint8_t method[] = {
        0x14U, 0x08U, '_', 'S', '5', '_', 0x00U, 0x00U
    };
    static const uint8_t unsupported[] = {
        0x08U, '_', 'S', '5', '_',
        0x12U, 0x04U, 0x02U, 0x0dU, 0x00U
    };
    uint8_t a = 0U;
    uint8_t b = 0U;

    require(boring_acpi_s5_parse(standard, sizeof(standard), &a, &b) &&
            (a == 5U) && (b == 5U), "static rooted package");
    require(boring_acpi_s5_parse(constants, sizeof(constants), &a, &b) &&
            (a == 0U) && (b == 1U), "ZeroOp/OneOp constants");
    require(!boring_acpi_s5_parse(malformed, sizeof(malformed), &a, &b),
            "malformed package length rejected");
    require(!boring_acpi_s5_parse(method, sizeof(method), &a, &b),
            "AML method is never executed or treated as static _S5");
    require(!boring_acpi_s5_parse(unsupported, sizeof(unsupported), &a, &b),
            "unsupported AML object rejected");
    (void)puts("M63 strict static ACPI _S5 parser host tests passed.");
    return 0;
}
