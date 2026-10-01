#include <stddef.h>

#include <boring/string.h>

size_t boring_strlen(const char *string) {
    size_t length = 0U;

    while (string[length] != '\0') {
        ++length;
    }

    return length;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *bytes = (unsigned char *)destination;
    const unsigned char fill = (unsigned char)value;
    size_t index;

    for (index = 0U; index < length; ++index) {
        bytes[index] = fill;
    }
    return destination;
}
