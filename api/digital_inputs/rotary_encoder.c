#include "../board.h"

// How the knob is read (quadrature decoding):
// Pins A and B together make a 2-bit state, A in bit 1 and B in bit 0. The knob
// rests at 11 in every detent, and one click walks through all four states:
//   clockwise:          11 -> 01 -> 00 -> 10 -> 11
//   counter-clockwise:  11 -> 10 -> 00 -> 01 -> 11
// Every step changes exactly one pin, and which pin changed first gives the
// direction. A click is counted when the knob lands back at 11.
#define DETENT_STATE 0x3

// rotary encoder struct (private to this file; other files use the functions in board.h)
typedef struct {
    pin_t pin_a, pin_b;          // A is the KY-040's CLK pin, B is its DT pin
    pin_t button_pin;
    uint8_t previous_state;      // last AB state (2 bits)
    int8_t  steps_since_detent;  // steps turned since the last detent: + clockwise, - counter-clockwise
    volatile int32_t position;   // clicks up from fully down: 0..ENCODER_STEPS
    volatile uint8_t is_pressed; // 1 while the button is held down
} encoder_t;

// all encoders on the board
static encoder_t encoders[NUM_ENCODERS] = {
    { .pin_a = { ENCODER_0_CLK_PIN }, .pin_b = { ENCODER_0_DT_PIN }, .button_pin = { ENCODER_0_BUTTON_PIN } },
    { .pin_a = { ENCODER_1_CLK_PIN }, .pin_b = { ENCODER_1_DT_PIN }, .button_pin = { ENCODER_1_BUTTON_PIN } },
    { .pin_a = { ENCODER_2_CLK_PIN }, .pin_b = { ENCODER_2_DT_PIN }, .button_pin = { ENCODER_2_BUTTON_PIN } },
    { .pin_a = { ENCODER_3_CLK_PIN }, .pin_b = { ENCODER_3_DT_PIN }, .button_pin = { ENCODER_3_BUTTON_PIN } },
};

// Pressing the knob pulls the button pin low, so pressed reads as 0.
// No debouncing yet.
static uint8_t button_is_held_down(const encoder_t *encoder) {
    return read_pin(encoder->button_pin) == 0;
}

static uint8_t read_state(const encoder_t *encoder) {
    return (read_pin(encoder->pin_a) << 1) | read_pin(encoder->pin_b);
}

// +1 for a clockwise step, -1 for a counter-clockwise step.
// 0 when both pins changed at once: a step was missed, so the direction is unknown.
static int8_t turn_direction(uint8_t previous_state, uint8_t current_state) {
    uint8_t changed_pins = previous_state ^ current_state;
    if (changed_pins == 0x3) return 0;

    // Turning clockwise, the new B always equals the old A.
    uint8_t previous_a = (previous_state >> 1) & 1;
    uint8_t current_b = current_state & 1;
    return (previous_a == current_b) ? 1 : -1;
}

// Called when the knob lands back in a detent. A full click is 4 steps, but 2 is
// enough to count it, so one missed step does not lose the click.
// Turning past either end of 0..ENCODER_STEPS does nothing.
static void count_click(encoder_t *encoder) {
    if      (encoder->steps_since_detent >=  2 && encoder->position < ENCODER_STEPS) encoder->position++;
    else if (encoder->steps_since_detent <= -2 && encoder->position > 0)             encoder->position--;
    encoder->steps_since_detent = 0;
}

static void poll_encoder(encoder_t *encoder) {
    // read the button first: it must update even when the knob has not moved
    encoder->is_pressed = button_is_held_down(encoder);

    uint8_t current_state = read_state(encoder);
    if (current_state == encoder->previous_state) return;

    encoder->steps_since_detent += turn_direction(encoder->previous_state, current_state);
    encoder->previous_state = current_state;

    if (current_state == DETENT_STATE) count_click(encoder);
}

// called every 1 ms by poll_inputs()
void poll_encoders(void) {
    for (int encoder_index = 0; encoder_index < NUM_ENCODERS; encoder_index++) {
        poll_encoder(&encoders[encoder_index]);
    }
}

// Knob value, 0.0 (fully down) to 1.0 (fully up).
// The position is still counted in whole clicks (an integer never drifts);
// only this getter turns it into the 0..1 the effects want.
float encoder_get_knob(int index) {
    return (float)encoders[index].position / ENCODER_STEPS;
}

// Moves a knob to the nearest click. An encoder has no end stops, so the
// firmware can put it anywhere, e.g. back where an effect was left.
void encoder_set_knob(int index, float knob) {
    if (knob < 0.0f) knob = 0.0f;
    if (knob > 1.0f) knob = 1.0f;
    encoders[index].position = (int32_t)(knob * ENCODER_STEPS + 0.5f);
}

uint8_t encoder_is_pressed(int index) {
    return encoders[index].is_pressed;
}
