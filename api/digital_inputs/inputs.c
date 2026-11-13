#include "../board.h"
#include <stdio.h>

// Prints an encoder's knob value (as a percentage) when its button is pressed.
// is_pressed stays 1 the whole time the button is held, so this only prints on
// the 0 -> 1 change (otherwise it would print every 1 ms while held).
static void print_encoder_presses(void)
{
    static uint8_t was_pressed[NUM_ENCODERS];
    for (int encoder_index = 0; encoder_index < NUM_ENCODERS; encoder_index++) {
        uint8_t pressed = encoder_is_pressed(encoder_index);
        if (pressed && !was_pressed[encoder_index]) {
            int knob_percent = (int)(encoder_get_knob(encoder_index) * 100.0f + 0.5f);
            printf("encoder %d pressed, knob: %d%%\n", encoder_index, knob_percent);
        }
        was_pressed[encoder_index] = pressed;
    }
}

// Polls every input. main() calls this once per millisecond from its loop.
// It is not an interrupt (SysTick belongs to the audio engine), so printf is
// safe in here.
void poll_inputs(void)
{
    poll_encoders();
    poll_buttons();
    print_encoder_presses();
}
