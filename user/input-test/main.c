#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define INPUT_TEST_LINE_CAPACITY 160U

int boring_main(int argc, char **argv);

static bool write_all(const char *text, size_t length) {
    size_t offset = 0U;

    while (offset < length) {
        const long result = boring_fd_write(BORING_FD_STDOUT,
                                            &text[offset], length - offset);
        if ((result <= 0L) || ((size_t)result > length - offset)) {
            return false;
        }
        offset += (size_t)result;
    }
    return true;
}

static bool write_text(const char *text) {
    return (text != NULL) && write_all(text, boring_strlen(text));
}

static bool literal_equals(const char *text, const char *literal) {
    size_t index = 0U;

    if ((text == NULL) || (literal == NULL)) {
        return false;
    }
    while ((text[index] != '\0') && (literal[index] != '\0')) {
        if (text[index] != literal[index]) {
            return false;
        }
        ++index;
    }
    return text[index] == literal[index];
}

static bool append_char(char *buffer, size_t capacity, size_t *length, char value) {
    if ((buffer == NULL) || (length == NULL) || (*length >= capacity)) {
        return false;
    }
    buffer[*length] = value;
    ++(*length);
    return true;
}

static bool append_text(char *buffer, size_t capacity, size_t *length,
                        const char *text) {
    size_t index;

    if (text == NULL) {
        return false;
    }
    for (index = 0U; text[index] != '\0'; ++index) {
        if (!append_char(buffer, capacity, length, text[index])) {
            return false;
        }
    }
    return true;
}

static bool append_i32(char *buffer, size_t capacity, size_t *length,
                       int32_t value) {
    char digits[11];
    size_t count = 0U;
    uint32_t magnitude;

    if (value < 0) {
        if (!append_char(buffer, capacity, length, '-')) {
            return false;
        }
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }
    do {
        digits[count] = (char)('0' + (char)(magnitude % 10U));
        ++count;
        magnitude /= 10U;
    } while (magnitude != 0U);
    while (count != 0U) {
        --count;
        if (!append_char(buffer, capacity, length, digits[count])) {
            return false;
        }
    }
    return true;
}

static const char *key_name(uint32_t code) {
    static const char *const letters[26] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
    };
    static const char *const digits[10] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
    };

    if ((code >= BORING_KEY_A) && (code <= BORING_KEY_Z)) {
        return letters[code - BORING_KEY_A];
    }
    if ((code >= BORING_KEY_0) && (code <= BORING_KEY_9)) {
        return digits[code - BORING_KEY_0];
    }
    switch (code) {
        case BORING_KEY_ESCAPE: return "ESCAPE";
        case BORING_KEY_TAB: return "TAB";
        case BORING_KEY_ENTER: return "ENTER";
        case BORING_KEY_BACKSPACE: return "BACKSPACE";
        case BORING_KEY_SPACE: return "SPACE";
        case BORING_KEY_MINUS: return "MINUS";
        case BORING_KEY_EQUAL: return "EQUAL";
        case BORING_KEY_LEFT_BRACKET: return "LEFT_BRACKET";
        case BORING_KEY_RIGHT_BRACKET: return "RIGHT_BRACKET";
        case BORING_KEY_BACKSLASH: return "BACKSLASH";
        case BORING_KEY_SEMICOLON: return "SEMICOLON";
        case BORING_KEY_APOSTROPHE: return "APOSTROPHE";
        case BORING_KEY_GRAVE: return "GRAVE";
        case BORING_KEY_COMMA: return "COMMA";
        case BORING_KEY_DOT: return "DOT";
        case BORING_KEY_SLASH: return "SLASH";
        case BORING_KEY_INSERT: return "INSERT";
        case BORING_KEY_DELETE: return "DELETE";
        case BORING_KEY_HOME: return "HOME";
        case BORING_KEY_END: return "END";
        case BORING_KEY_PAGE_UP: return "PAGE_UP";
        case BORING_KEY_PAGE_DOWN: return "PAGE_DOWN";
        case BORING_KEY_LEFT: return "LEFT";
        case BORING_KEY_RIGHT: return "RIGHT";
        case BORING_KEY_UP: return "UP";
        case BORING_KEY_DOWN: return "DOWN";
        case BORING_KEY_F1: return "F1";
        case BORING_KEY_F2: return "F2";
        case BORING_KEY_F3: return "F3";
        case BORING_KEY_F4: return "F4";
        case BORING_KEY_F5: return "F5";
        case BORING_KEY_F6: return "F6";
        case BORING_KEY_F7: return "F7";
        case BORING_KEY_F8: return "F8";
        case BORING_KEY_F9: return "F9";
        case BORING_KEY_F10: return "F10";
        case BORING_KEY_F11: return "F11";
        case BORING_KEY_F12: return "F12";
        case BORING_KEY_LEFT_SHIFT: return "LEFT_SHIFT";
        case BORING_KEY_RIGHT_SHIFT: return "RIGHT_SHIFT";
        case BORING_KEY_LEFT_CTRL: return "LEFT_CTRL";
        case BORING_KEY_RIGHT_CTRL: return "RIGHT_CTRL";
        case BORING_KEY_LEFT_ALT: return "LEFT_ALT";
        case BORING_KEY_RIGHT_ALT: return "RIGHT_ALT";
        case BORING_KEY_LEFT_SUPER: return "LEFT_SUPER";
        case BORING_KEY_RIGHT_SUPER: return "RIGHT_SUPER";
        default: return "UNKNOWN";
    }
}

static bool append_modifiers(char *buffer, size_t capacity, size_t *length,
                             uint32_t modifiers) {
    bool first = true;

    if (modifiers == 0U) {
        return append_text(buffer, capacity, length, "NONE");
    }
#define APPEND_MOD(bit, name) \
    do { \
        if ((modifiers & (bit)) != 0U) { \
            if ((!first) && !append_char(buffer, capacity, length, '+')) return false; \
            if (!append_text(buffer, capacity, length, (name))) return false; \
            first = false; \
        } \
    } while (0)
    APPEND_MOD(BORING_MOD_SHIFT, "SHIFT");
    APPEND_MOD(BORING_MOD_CTRL, "CTRL");
    APPEND_MOD(BORING_MOD_ALT, "ALT");
    APPEND_MOD(BORING_MOD_SUPER, "SUPER");
#undef APPEND_MOD
    return true;
}

static bool print_event(const struct boring_input_event *event) {
    char line[INPUT_TEST_LINE_CAPACITY];
    size_t length = 0U;

    if (event == NULL) {
        return false;
    }
    if (event->type == BORING_INPUT_EVENT_KEY) {
        if (!append_text(line, sizeof(line), &length, "KEY ") ||
            !append_text(line, sizeof(line), &length,
                         (event->value1 == BORING_KEY_DOWN_VALUE) ? "DOWN " : "UP ") ||
            !append_text(line, sizeof(line), &length, key_name(event->code)) ||
            !append_text(line, sizeof(line), &length, " mods=") ||
            !append_modifiers(line, sizeof(line), &length, event->modifiers)) {
            return false;
        }
        if ((event->flags & BORING_INPUT_FLAG_REPEAT) != 0U) {
            if (!append_text(line, sizeof(line), &length, " repeat")) {
                return false;
            }
        }
    } else if (event->type == BORING_INPUT_EVENT_MOUSE_MOVE) {
        if (!append_text(line, sizeof(line), &length, "MOUSE MOVE dx=") ||
            !append_i32(line, sizeof(line), &length, event->value1) ||
            !append_text(line, sizeof(line), &length, " dy=") ||
            !append_i32(line, sizeof(line), &length, event->value2)) {
            return false;
        }
    } else if (event->type == BORING_INPUT_EVENT_MOUSE_BUTTON) {
        const char *button = (event->code == BORING_MOUSE_BUTTON_LEFT) ? "LEFT" :
                             (event->code == BORING_MOUSE_BUTTON_MIDDLE) ? "MIDDLE" :
                             (event->code == BORING_MOUSE_BUTTON_RIGHT) ? "RIGHT" : "UNKNOWN";
        if (!append_text(line, sizeof(line), &length, "MOUSE BUTTON ") ||
            !append_text(line, sizeof(line), &length, button) ||
            !append_char(line, sizeof(line), &length, ' ') ||
            !append_text(line, sizeof(line), &length,
                         (event->value1 != 0) ? "DOWN" : "UP")) {
            return false;
        }
    } else {
        return true;
    }
    return append_text(line, sizeof(line), &length, "\r\n") && write_all(line, length);
}

int boring_main(int argc, char **argv) {
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    bool teardown_mode = false;
    bool saw_super_q = false;
    bool saw_super_enter = false;
    bool saw_mouse_move = false;
    bool saw_left_down = false;
    bool saw_left_up = false;

    if ((argc == 2) && (argv != NULL) &&
        literal_equals(argv[1], "--teardown")) {
        teardown_mode = true;
    } else if ((argc != 1) || (argv == NULL)) {
        (void)write_text("input-test: usage: input-test [--teardown]\r\n");
        boring_exit(2);
    }

    if (boring_input_claim() != 0L) {
        (void)write_text("input-test: claim failed\r\n");
        boring_exit(1);
    }
    (void)write_text("input-test: input claimed\r\n");
    (void)write_text("input-test: waiting for keyboard/mouse events\r\n");

    for (;;) {
        const long read_count = boring_input_read(events, BORING_INPUT_READ_MAX);
        size_t index;

        if ((read_count <= 0L) ||
            ((size_t)read_count > (size_t)BORING_INPUT_READ_MAX)) {
            (void)write_text("input-test: read failed\r\n");
            boring_exit(1);
        }
        for (index = 0U; index < (size_t)read_count; ++index) {
            const struct boring_input_event *const event = &events[index];

            if (!print_event(event)) {
                boring_exit(1);
            }
            if (teardown_mode) {
                (void)write_text("input-test: exiting without release\r\n");
                boring_exit(0);
            }
            if ((event->type == BORING_INPUT_EVENT_KEY) &&
                (event->value1 == BORING_KEY_DOWN_VALUE) &&
                ((event->modifiers & BORING_MOD_SUPER) != 0U)) {
                if (event->code == BORING_KEY_Q) {
                    saw_super_q = true;
                } else if (event->code == BORING_KEY_ENTER) {
                    saw_super_enter = true;
                }
            } else if ((event->type == BORING_INPUT_EVENT_MOUSE_MOVE) &&
                       ((event->value1 != 0) || (event->value2 != 0))) {
                saw_mouse_move = true;
            } else if ((event->type == BORING_INPUT_EVENT_MOUSE_BUTTON) &&
                       (event->code == BORING_MOUSE_BUTTON_LEFT)) {
                if (event->value1 != 0) {
                    saw_left_down = true;
                } else {
                    saw_left_up = true;
                }
            }
        }
        if (saw_super_q && saw_super_enter && saw_mouse_move &&
            saw_left_down && saw_left_up) {
            break;
        }
    }

    (void)write_text("input-test: witness complete\r\n");
    if (boring_input_release() != 0L) {
        (void)write_text("input-test: release failed\r\n");
        boring_exit(1);
    }
    (void)write_text("input-test: input released\r\n");
    boring_exit(0);
}
