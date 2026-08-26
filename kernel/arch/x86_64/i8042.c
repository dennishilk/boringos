#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/i8042.h>
#include <boring/input.h>
#include <boring/io.h>
#include <boring/ps2_keyboard.h>
#include <boring/ps2_mouse.h>

#define I8042_DATA_PORT 0x60U
#define I8042_STATUS_COMMAND_PORT 0x64U
#define I8042_STATUS_OUTPUT_FULL (1U << 0)
#define I8042_STATUS_INPUT_FULL (1U << 1)
#define I8042_STATUS_AUX_DATA (1U << 5)
#define I8042_STATUS_TIMEOUT (1U << 6)
#define I8042_STATUS_PARITY (1U << 7)
#define I8042_COMMAND_READ_CONFIG 0x20U
#define I8042_COMMAND_WRITE_CONFIG 0x60U
#define I8042_COMMAND_DISABLE_KEYBOARD 0xadU
#define I8042_COMMAND_ENABLE_KEYBOARD 0xaeU
#define I8042_COMMAND_DISABLE_MOUSE 0xa7U
#define I8042_COMMAND_ENABLE_MOUSE 0xa8U
#define I8042_COMMAND_WRITE_MOUSE 0xd4U
#define I8042_CONFIG_IRQ1 (1U << 0)
#define I8042_CONFIG_IRQ12 (1U << 1)
#define I8042_CONFIG_KEYBOARD_DISABLE (1U << 4)
#define I8042_CONFIG_MOUSE_DISABLE (1U << 5)
#define I8042_CONFIG_TRANSLATION (1U << 6)
#define PS2_COMMAND_SET_DEFAULTS 0xf6U
#define PS2_COMMAND_ENABLE_REPORTING 0xf4U
#define PS2_RESPONSE_ACK 0xfaU
#define PS2_RESPONSE_RESEND 0xfeU
#define I8042_WAIT_LIMIT 100000U
#define I8042_FLUSH_LIMIT 64U
#define PS2_COMMAND_ATTEMPTS 3U

static struct i8042_state active_state;
static struct ps2_keyboard_decoder keyboard_decoder;
static struct ps2_mouse_decoder mouse_decoder;

static bool i8042_wait_input_empty(void) {
    uint32_t attempt;

    for (attempt = 0U; attempt < I8042_WAIT_LIMIT; ++attempt) {
        if ((x86_64_in8((uint16_t)I8042_STATUS_COMMAND_PORT) &
             (uint8_t)I8042_STATUS_INPUT_FULL) == 0U) {
            return true;
        }
        x86_64_pause();
    }
    return false;
}

static bool i8042_write_command(uint8_t command) {
    if (!i8042_wait_input_empty()) {
        return false;
    }
    x86_64_out8((uint16_t)I8042_STATUS_COMMAND_PORT, command);
    return true;
}

static bool i8042_write_data(uint8_t value) {
    if (!i8042_wait_input_empty()) {
        return false;
    }
    x86_64_out8((uint16_t)I8042_DATA_PORT, value);
    return true;
}

static bool i8042_read_output(bool auxiliary, uint8_t *value) {
    uint32_t attempt;

    if (value == NULL) {
        return false;
    }
    for (attempt = 0U; attempt < I8042_WAIT_LIMIT; ++attempt) {
        const uint8_t status =
            x86_64_in8((uint16_t)I8042_STATUS_COMMAND_PORT);

        if ((status & (uint8_t)I8042_STATUS_OUTPUT_FULL) != 0U) {
            const uint8_t data = x86_64_in8((uint16_t)I8042_DATA_PORT);
            const bool source_aux =
                (status & (uint8_t)I8042_STATUS_AUX_DATA) != 0U;

            if ((status & (uint8_t)(I8042_STATUS_TIMEOUT |
                                     I8042_STATUS_PARITY)) != 0U) {
                continue;
            }
            if (source_aux != auxiliary) {
                continue;
            }
            *value = data;
            return true;
        }
        x86_64_pause();
    }
    return false;
}

static void i8042_flush(void) {
    uint32_t count;

    for (count = 0U; count < I8042_FLUSH_LIMIT; ++count) {
        const uint8_t status =
            x86_64_in8((uint16_t)I8042_STATUS_COMMAND_PORT);
        if ((status & (uint8_t)I8042_STATUS_OUTPUT_FULL) == 0U) {
            return;
        }
        (void)x86_64_in8((uint16_t)I8042_DATA_PORT);
    }
}

static bool i8042_read_config(uint8_t *config) {
    return i8042_write_command((uint8_t)I8042_COMMAND_READ_CONFIG) &&
           i8042_read_output(false, config);
}

static bool i8042_write_config(uint8_t config) {
    return i8042_write_command((uint8_t)I8042_COMMAND_WRITE_CONFIG) &&
           i8042_write_data(config);
}

static bool i8042_send_device(bool auxiliary, uint8_t command) {
    uint32_t attempt;

    for (attempt = 0U; attempt < PS2_COMMAND_ATTEMPTS; ++attempt) {
        uint8_t response;

        if (auxiliary &&
            !i8042_write_command((uint8_t)I8042_COMMAND_WRITE_MOUSE)) {
            return false;
        }
        if (!i8042_write_data(command) ||
            !i8042_read_output(auxiliary, &response)) {
            return false;
        }
        if (response == (uint8_t)PS2_RESPONSE_ACK) {
            return true;
        }
        if (response != (uint8_t)PS2_RESPONSE_RESEND) {
            return false;
        }
    }
    return false;
}

static bool i8042_keyboard_init(void) {
    if (!i8042_write_command((uint8_t)I8042_COMMAND_ENABLE_KEYBOARD)) {
        return false;
    }
    return i8042_send_device(false, (uint8_t)PS2_COMMAND_SET_DEFAULTS) &&
           i8042_send_device(false, (uint8_t)PS2_COMMAND_ENABLE_REPORTING);
}

static bool i8042_mouse_init(void) {
    if (!i8042_write_command((uint8_t)I8042_COMMAND_ENABLE_MOUSE)) {
        return false;
    }
    return i8042_send_device(true, (uint8_t)PS2_COMMAND_SET_DEFAULTS) &&
           i8042_send_device(true, (uint8_t)PS2_COMMAND_ENABLE_REPORTING);
}

bool i8042_init(struct i8042_state *state) {
    uint8_t config;
    bool keyboard_online;
    bool mouse_online;

    if (state == NULL) {
        return false;
    }
    active_state.controller_available = false;
    active_state.keyboard_online = false;
    active_state.mouse_online = false;
    ps2_keyboard_decoder_init(&keyboard_decoder);
    ps2_mouse_decoder_init(&mouse_decoder);

    if (!i8042_write_command((uint8_t)I8042_COMMAND_DISABLE_KEYBOARD) ||
        !i8042_write_command((uint8_t)I8042_COMMAND_DISABLE_MOUSE)) {
        *state = active_state;
        return false;
    }
    i8042_flush();
    if (!i8042_read_config(&config)) {
        *state = active_state;
        return false;
    }
    active_state.controller_available = true;

    config = (uint8_t)(config &
              (uint8_t)~(I8042_CONFIG_IRQ1 | I8042_CONFIG_IRQ12));
    config = (uint8_t)(config | (uint8_t)I8042_CONFIG_TRANSLATION);
    if (!i8042_write_config(config)) {
        *state = active_state;
        return false;
    }

    keyboard_online = i8042_keyboard_init();
    if (!keyboard_online) {
        (void)i8042_write_command((uint8_t)I8042_COMMAND_DISABLE_KEYBOARD);
    }
    mouse_online = i8042_mouse_init();
    if (!mouse_online) {
        (void)i8042_write_command((uint8_t)I8042_COMMAND_DISABLE_MOUSE);
    }

    config = (uint8_t)(config | (uint8_t)I8042_CONFIG_TRANSLATION);
    config = (uint8_t)(config &
              (uint8_t)~(I8042_CONFIG_IRQ1 | I8042_CONFIG_IRQ12));
    if (keyboard_online) {
        config = (uint8_t)(config &
                  (uint8_t)~I8042_CONFIG_KEYBOARD_DISABLE);
        config = (uint8_t)(config | (uint8_t)I8042_CONFIG_IRQ1);
    } else {
        config = (uint8_t)(config | (uint8_t)I8042_CONFIG_KEYBOARD_DISABLE);
    }
    if (mouse_online) {
        config = (uint8_t)(config & (uint8_t)~I8042_CONFIG_MOUSE_DISABLE);
        config = (uint8_t)(config | (uint8_t)I8042_CONFIG_IRQ12);
    } else {
        config = (uint8_t)(config | (uint8_t)I8042_CONFIG_MOUSE_DISABLE);
    }
    if (!i8042_write_config(config)) {
        keyboard_online = false;
        mouse_online = false;
    }

    active_state.keyboard_online = keyboard_online;
    active_state.mouse_online = mouse_online;
    *state = active_state;
    return active_state.controller_available;
}

static void i8042_emit_mouse_packet(const struct ps2_mouse_packet *packet) {
    static const uint8_t bits[3] = {
        PS2_MOUSE_BUTTON_LEFT_BIT,
        PS2_MOUSE_BUTTON_MIDDLE_BIT,
        PS2_MOUSE_BUTTON_RIGHT_BIT
    };
    static const uint32_t buttons[3] = {
        BORING_MOUSE_BUTTON_LEFT,
        BORING_MOUSE_BUTTON_MIDDLE,
        BORING_MOUSE_BUTTON_RIGHT
    };
    size_t index;

    if (packet->movement_valid &&
        ((packet->dx != 0) || (packet->dy != 0))) {
        (void)boring_input_submit_mouse_move(packet->dx, packet->dy);
    }
    for (index = 0U; index < 3U; ++index) {
        if ((packet->changed_buttons & bits[index]) != 0U) {
            (void)boring_input_submit_mouse_button(
                buttons[index], (packet->buttons & bits[index]) != 0U);
        }
    }
}

bool i8042_handle_irq(uint8_t irq_number) {
    const uint8_t status = x86_64_in8((uint16_t)I8042_STATUS_COMMAND_PORT);
    uint8_t data;
    bool source_aux;

    if ((irq_number != 1U) && (irq_number != 12U)) {
        return false;
    }
    if ((status & (uint8_t)I8042_STATUS_OUTPUT_FULL) == 0U) {
        return false;
    }
    data = x86_64_in8((uint16_t)I8042_DATA_PORT);
    source_aux = (status & (uint8_t)I8042_STATUS_AUX_DATA) != 0U;
    if ((status & (uint8_t)(I8042_STATUS_TIMEOUT |
                             I8042_STATUS_PARITY)) != 0U) {
        if (source_aux) {
            ps2_mouse_decoder_init(&mouse_decoder);
        } else {
            ps2_keyboard_decoder_init(&keyboard_decoder);
        }
        return true;
    }

    if (source_aux) {
        struct ps2_mouse_packet packet;

        if (!active_state.mouse_online) {
            return true;
        }
        if (ps2_mouse_decoder_feed(&mouse_decoder, data, &packet)) {
            i8042_emit_mouse_packet(&packet);
        }
    } else {
        struct ps2_keyboard_transition transition;

        if (!active_state.keyboard_online) {
            return true;
        }
        if (ps2_keyboard_decoder_feed(&keyboard_decoder, data, &transition)) {
            (void)boring_input_submit_key(transition.code, transition.down);
        }
    }
    return true;
}
