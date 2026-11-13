#include "../board.h"

typedef struct {
    XMC_GPIO_PORT_t *port;
    uint8_t pin;
    volatile uint8_t is_pressed; // 1 while the button is held down
} button_t;

static button_t buttons[NUM_BUTTONS] = {
    { .port = XMC_GPIO_PORT0, .pin = 15 },  // button 0: P0.15
    { .port = XMC_GPIO_PORT0, .pin = 14 },  // button 1: P0.14
    { .port = XMC_GPIO_PORT0, .pin = 13 },  // button 2: P0.13
    { .port = XMC_GPIO_PORT0, .pin = 12 },  // button 3: P0.12
};


// no debouncing yet
static void poll_button(button_t *button) {
    button->is_pressed = (XMC_GPIO_GetInput(button->port, button->pin) == 0);
}

void poll_buttons(void) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        poll_button(&buttons[i]);
    }
}

uint8_t button_is_pressed(int index) {
    return buttons[index].is_pressed;
}
