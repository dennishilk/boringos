#include <stddef.h>
#include <stdint.h>

#include <boring/memory.h>

void *boring_memcpy(void *destination, const void *source, size_t length) {
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;
    size_t index;

    for (index = 0U; index < length; ++index) {
        output[index] = input[index];
    }

    return destination;
}

void *boring_memset(void *destination, int value, size_t length) {
    uint8_t *output = (uint8_t *)destination;
    const uint8_t byte = (uint8_t)value;
    size_t index;

    for (index = 0U; index < length; ++index) {
        output[index] = byte;
    }

    return destination;
}
