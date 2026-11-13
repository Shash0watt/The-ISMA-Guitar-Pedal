#include "../board.h"

typedef struct {
    pin_t pin;
    volatile uint8_t is_pressed; // 1 while the button is held down
} button_t;

static button_t buttons[NUM_BUTTONS] = {
    { .pin = { BUTTON_0_PIN } },
    { .pin = { BUTTON_1_PIN } },
    { .pin = { BUTTON_2_PIN } },
    { .pin = { BUTTON_3_PIN } },
};

// Pressing the button pulls its pin low, so pressed reads as 0.
// No debouncing yet.
static void poll_button(button_t *button) {
    button->is_pressed = (read_pin(button->pin) == 0);
}

void poll_buttons(void) {
    for (int button_index = 0; button_index < NUM_BUTTONS; button_index++) {
        poll_button(&buttons[button_index]);
    }
}

uint8_t button_is_pressed(int index) {
    return buttons[index].is_pressed;
}
