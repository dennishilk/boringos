#include <stddef.h>

#include <boring/string.h>

size_t boring_strlen(const char *string) {
    size_t length = 0U;

    while (string[length] != '\0') {
        ++length;
    }

    return length;
}
