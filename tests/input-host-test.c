#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/cpu.h>
#include <boring/input.h>
#include <boring/ps2_keyboard.h>
#include <boring/ps2_mouse.h>

static bool test_interrupts_enabled = true;
static unsigned failures;

void x86_64_interrupts_disable(void) {
    test_interrupts_enabled = false;
}

void x86_64_interrupts_enable(void) {
    test_interrupts_enabled = true;
}

bool x86_64_interrupts_enabled(void) {
    return test_interrupts_enabled;
}

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "input-host-test: FAIL: %s\n", name);
        ++failures;
    }
}

static bool key_feed(struct ps2_keyboard_decoder *decoder,
                     uint8_t byte,
                     uint32_t code,
                     bool down) {
    struct ps2_keyboard_transition transition;

    return ps2_keyboard_decoder_feed(decoder, byte, &transition) &&
           transition.code == code && transition.down == down;
}

static void keyboard_tests(void) {
    struct ps2_keyboard_decoder decoder;
    struct ps2_keyboard_transition transition;
    struct boring_input_event events[8];
    size_t count = 0U;

    ps2_keyboard_decoder_init(&decoder);
    check(key_feed(&decoder, 0x1eU, BORING_KEY_A, true), "A make");
    check(key_feed(&decoder, 0x9eU, BORING_KEY_A, false), "A break");
    check(key_feed(&decoder, 0x10U, BORING_KEY_Q, true), "Q make");
    check(key_feed(&decoder, 0x90U, BORING_KEY_Q, false), "Q break");
    check(key_feed(&decoder, 0x1cU, BORING_KEY_ENTER, true), "Enter make");
    check(key_feed(&decoder, 0x9cU, BORING_KEY_ENTER, false), "Enter break");
    check(key_feed(&decoder, 0x01U, BORING_KEY_ESCAPE, true), "Escape");
    check(key_feed(&decoder, 0x2aU, BORING_KEY_LEFT_SHIFT, true), "Shift");
    check(key_feed(&decoder, 0x1dU, BORING_KEY_LEFT_CTRL, true), "left Ctrl");
    check(key_feed(&decoder, 0x38U, BORING_KEY_LEFT_ALT, true), "left Alt");
    check(key_feed(&decoder, 0x3bU, BORING_KEY_F1, true), "F1");
    check(key_feed(&decoder, 0x58U, BORING_KEY_F12, true), "F12");

    check(!ps2_keyboard_decoder_feed(&decoder, 0xe0U, &transition), "E0 prefix");
    check(key_feed(&decoder, 0x48U, BORING_KEY_UP, true), "E0 arrow make");
    check(!ps2_keyboard_decoder_feed(&decoder, 0xe0U, &transition), "E0 break prefix");
    check(key_feed(&decoder, 0xc8U, BORING_KEY_UP, false), "E0 arrow break");
    check(!ps2_keyboard_decoder_feed(&decoder, 0xe0U, &transition), "Super prefix");
    check(key_feed(&decoder, 0x5bU, BORING_KEY_LEFT_SUPER, true), "left Super");
    check(!ps2_keyboard_decoder_feed(&decoder, 0xe0U, &transition), "right Super prefix");
    check(key_feed(&decoder, 0x5cU, BORING_KEY_RIGHT_SUPER, true), "right Super");
    check(!ps2_keyboard_decoder_feed(&decoder, 0xe0U, &transition), "right Ctrl prefix");
    check(key_feed(&decoder, 0x1dU, BORING_KEY_RIGHT_CTRL, true), "right Ctrl");
    check(!ps2_keyboard_decoder_feed(&decoder, 0xe0U, &transition), "right Alt prefix");
    check(key_feed(&decoder, 0x38U, BORING_KEY_RIGHT_ALT, true), "right Alt");

    check(!ps2_keyboard_decoder_feed(&decoder, 0xe0U, &transition), "malformed E0 prefix");
    check(!ps2_keyboard_decoder_feed(&decoder, 0x00U, &transition), "malformed E0 ignored");
    check(key_feed(&decoder, 0x1eU, BORING_KEY_A, true), "malformed E0 recovery");

    check(boring_input_claim(11ULL) == BORING_INPUT_RESULT_OK, "keyboard owner claim");
    check(boring_input_submit_key(BORING_KEY_LEFT_SUPER, true), "Super down enqueue");
    check(boring_input_submit_key(BORING_KEY_Q, true), "Super+Q enqueue");
    check(boring_input_submit_key(BORING_KEY_Q, true), "Q repeat enqueue");
    check(boring_input_submit_key(BORING_KEY_Q, false), "Q up enqueue");
    check(boring_input_submit_key(BORING_KEY_LEFT_SUPER, false), "Super up enqueue");
    check(boring_input_submit_key(BORING_KEY_LEFT_SUPER, true), "Super down 2 enqueue");
    check(boring_input_submit_key(BORING_KEY_ENTER, true), "Super+Enter enqueue");
    check(boring_input_submit_key(BORING_KEY_ENTER, false), "Enter up enqueue");
    check(boring_input_read(11ULL, events, 8U, &count) == BORING_INPUT_RESULT_OK &&
          count == 8U, "keyboard event read");
    check(events[0].code == BORING_KEY_LEFT_SUPER &&
          events[0].value1 == BORING_KEY_DOWN_VALUE &&
          events[0].modifiers == BORING_MOD_SUPER, "modifier after Super down");
    check(events[1].code == BORING_KEY_Q &&
          events[1].modifiers == BORING_MOD_SUPER && events[1].flags == 0U,
          "Super+Q witness");
    check(events[2].code == BORING_KEY_Q &&
          events[2].flags == BORING_INPUT_FLAG_REPEAT, "repeat flag");
    check(events[4].code == BORING_KEY_LEFT_SUPER &&
          events[4].modifiers == 0U, "modifier after Super up");
    check(events[6].code == BORING_KEY_ENTER &&
          events[6].modifiers == BORING_MOD_SUPER, "Super+Enter witness");
    check(boring_input_release(11ULL) == BORING_INPUT_RESULT_OK, "keyboard release");
}

static bool mouse_packet(struct ps2_mouse_decoder *decoder,
                         uint8_t first, uint8_t second, uint8_t third,
                         struct ps2_mouse_packet *packet) {
    return !ps2_mouse_decoder_feed(decoder, first, packet) &&
           !ps2_mouse_decoder_feed(decoder, second, packet) &&
           ps2_mouse_decoder_feed(decoder, third, packet);
}

static void mouse_tests(void) {
    struct ps2_mouse_decoder decoder;
    struct ps2_mouse_packet packet;

    ps2_mouse_decoder_init(&decoder);
    check(mouse_packet(&decoder, 0x08U, 0U, 0U, &packet) &&
          packet.dx == 0 && packet.dy == 0 && packet.movement_valid,
          "zero movement");
    check(mouse_packet(&decoder, 0x08U, 12U, 0U, &packet) && packet.dx == 12,
          "positive X");
    check(mouse_packet(&decoder, 0x18U, 0xf4U, 0U, &packet) && packet.dx == -12,
          "negative X sign extension");
    check(mouse_packet(&decoder, 0x28U, 0U, 0xfbU, &packet) && packet.dy == 5,
          "positive BoringOS Y");
    check(mouse_packet(&decoder, 0x08U, 0U, 5U, &packet) && packet.dy == -5,
          "negative BoringOS Y");
    check(mouse_packet(&decoder, 0x38U, 0xfeU, 0xfdU, &packet) &&
          packet.dx == -2 && packet.dy == 3, "combined XY");

    ps2_mouse_decoder_init(&decoder);
    check(mouse_packet(&decoder, 0x09U, 0U, 0U, &packet) &&
          packet.changed_buttons == PS2_MOUSE_BUTTON_LEFT_BIT &&
          packet.buttons == PS2_MOUSE_BUTTON_LEFT_BIT, "left down");
    check(mouse_packet(&decoder, 0x08U, 0U, 0U, &packet) &&
          packet.changed_buttons == PS2_MOUSE_BUTTON_LEFT_BIT && packet.buttons == 0U,
          "left up");
    check(mouse_packet(&decoder, 0x0aU, 0U, 0U, &packet) &&
          packet.changed_buttons == PS2_MOUSE_BUTTON_RIGHT_BIT, "right down");
    check(mouse_packet(&decoder, 0x08U, 0U, 0U, &packet) &&
          packet.changed_buttons == PS2_MOUSE_BUTTON_RIGHT_BIT, "right up");
    check(mouse_packet(&decoder, 0x0cU, 0U, 0U, &packet) &&
          packet.changed_buttons == PS2_MOUSE_BUTTON_MIDDLE_BIT, "middle down");
    check(mouse_packet(&decoder, 0x08U, 0U, 0U, &packet) &&
          packet.changed_buttons == PS2_MOUSE_BUTTON_MIDDLE_BIT, "middle up");

    ps2_mouse_decoder_init(&decoder);
    check(!ps2_mouse_decoder_feed(&decoder, 0x00U, &packet), "malformed first byte");
    check(mouse_packet(&decoder, 0x08U, 1U, 2U, &packet) &&
          packet.dx == 1 && packet.dy == -2, "mouse resynchronization");
    check(mouse_packet(&decoder, 0x48U, 127U, 0U, &packet) &&
          packet.overflow && !packet.movement_valid && packet.dx == 0,
          "X overflow");
    check(mouse_packet(&decoder, 0x88U, 0U, 127U, &packet) &&
          packet.overflow && !packet.movement_valid && packet.dy == 0,
          "Y overflow");
}

static void queue_tests(void) {
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    struct boring_input_stats stats;
    size_t count = 99U;
    size_t drained = 0U;
    size_t index;
    bool released = false;

    check(boring_input_claim(21ULL) == BORING_INPUT_RESULT_OK, "owner claim");
    check(boring_input_claim(21ULL) == BORING_INPUT_RESULT_OK, "same owner claim");
    check(boring_input_claim(22ULL) == BORING_INPUT_RESULT_BUSY, "second owner rejected");
    check(boring_input_read(22ULL, events, 1U, &count) == BORING_INPUT_RESULT_ACCESS,
          "non-owner read rejected");
    check(boring_input_read(21ULL, events, 1U, &count) == BORING_INPUT_RESULT_OK &&
          count == 0U, "empty queue");
    check(boring_input_wait_prepare(21ULL), "owner wait prepare");
    check(boring_input_get_stats(&stats) && stats.waiting, "owner wait state");
    boring_input_wait_cancel(21ULL);
    check(boring_input_get_stats(&stats) && !stats.waiting, "owner wait cancel");
    check(boring_input_submit_key(BORING_KEY_A, true), "single enqueue");
    check(boring_input_read(21ULL, events, 1U, &count) == BORING_INPUT_RESULT_OK &&
          count == 1U && events[0].code == BORING_KEY_A, "single dequeue");

    for (index = 0U; index < (size_t)BORING_INPUT_QUEUE_CAPACITY; ++index) {
        check(boring_input_submit_key(BORING_KEY_A, (index & 1U) == 0U),
              "fill queue");
    }
    check(!boring_input_submit_key(BORING_KEY_Q, true), "drop newest on overflow");
    check(boring_input_get_stats(&stats) &&
          stats.queued_events == (size_t)BORING_INPUT_QUEUE_CAPACITY &&
          stats.dropped_events == 1ULL, "overflow accounting");
    while (drained < (size_t)BORING_INPUT_QUEUE_CAPACITY) {
        check(boring_input_read(21ULL, events, BORING_INPUT_READ_MAX, &count) ==
              BORING_INPUT_RESULT_OK && count != 0U, "drain chunk");
        drained += count;
    }
    check(drained == (size_t)BORING_INPUT_QUEUE_CAPACITY, "FIFO drain count");
    check(boring_input_get_stats(&stats) && stats.queued_events == 0U,
          "queue empty after drain");

    for (index = 0U; index < 40U; ++index) {
        check(boring_input_submit_mouse_move((int32_t)index + 1, 0), "wrap enqueue A");
    }
    check(boring_input_read(21ULL, events, BORING_INPUT_READ_MAX, &count) ==
          BORING_INPUT_RESULT_OK && count == BORING_INPUT_READ_MAX,
          "wrap first drain");
    for (index = 0U; index < 20U; ++index) {
        check(boring_input_submit_mouse_move((int32_t)index + 100, 0), "wrap enqueue B");
    }
    drained = 0U;
    do {
        check(boring_input_read(21ULL, events, BORING_INPUT_READ_MAX, &count) ==
              BORING_INPUT_RESULT_OK, "wrap drain");
        drained += count;
    } while (count != 0U);
    check(drained == 44U, "wrap-around preserved");

    check(boring_input_release(22ULL) == BORING_INPUT_RESULT_ACCESS,
          "non-owner release rejected");
    check(boring_input_process_teardown(21ULL, &released) && released,
          "owner teardown");
    check(boring_input_claim(22ULL) == BORING_INPUT_RESULT_OK,
          "reclaim after teardown");
    check(boring_input_release(22ULL) == BORING_INPUT_RESULT_OK, "release");
    check(boring_input_claim(23ULL) == BORING_INPUT_RESULT_OK, "reuse after clear");
    check(boring_input_get_stats(&stats) && stats.queued_events == 0U &&
          stats.modifiers == 0U, "clear semantics");
    check(boring_input_release(23ULL) == BORING_INPUT_RESULT_OK, "final release");
}

int main(void) {
    check(boring_input_init(), "input init");
    keyboard_tests();
    mouse_tests();
    queue_tests();
    if (failures != 0U) {
        (void)fprintf(stderr, "input-host-test: %u failure(s)\n", failures);
        return 1;
    }
    (void)printf("Native input decoder and queue host tests passed.\n");
    return 0;
}
