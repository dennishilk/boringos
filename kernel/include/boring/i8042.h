#ifndef BORING_I8042_H
#define BORING_I8042_H

#include <stdbool.h>
#include <stdint.h>

struct i8042_state {
    bool controller_available;
    bool keyboard_online;
    bool mouse_online;
};

bool i8042_init(struct i8042_state *state);
bool i8042_handle_irq(uint8_t irq_number);

#endif
