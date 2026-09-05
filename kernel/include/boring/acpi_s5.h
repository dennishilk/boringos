#ifndef BORING_ACPI_S5_H
#define BORING_ACPI_S5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool boring_acpi_s5_parse(const uint8_t *aml,
                          size_t aml_length,
                          uint8_t *sleep_type_a,
                          uint8_t *sleep_type_b);

#endif
