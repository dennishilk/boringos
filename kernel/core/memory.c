#include <stddef.h>
#include <stdint.h>

#include <boring/memory.h>

void *memcpy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;

    for (index = 0U; index < length; ++index) {
        out[index] = in[index];
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t fill = (uint8_t)value;
    size_t index;

    for (index = 0U; index < length; ++index) {
        out[index] = fill;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;

    if ((out == in) || (length == 0U)) {
        return destination;
    }
    if (out < in) {
        for (index = 0U; index < length; ++index) {
            out[index] = in[index];
        }
    } else {
        index = length;
        while (index != 0U) {
            --index;
            out[index] = in[index];
        }
    }
    return destination;
}
