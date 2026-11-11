#include "../board.h"
#include <stdio.h>

// Polls every input. main() calls this once per millisecond from its loop.
//
// This used to be SysTick_Handler with its own 1 ms SysTick, but the audio
// engine (api/audio) needs SysTick for the 96 kHz sample interrupt. Polling
// from the main loop keeps the inputs off the audio interrupt entirely, and
// printf is safe here (it isn't inside an interrupt).
void poll_inputs(void)
{
    poll_encoders();
    poll_buttons();

    // Example:
    // print the encoder's current position when its button is pressed.
    // is_pressed stays 1 the whole time it's held, so only print on the
    // 0 -> 1 change (otherwise it would print every 1 ms while held).
    static uint8_t was_pressed = 0;
    uint8_t pressed = encoder_is_pressed(0);
    if (pressed && !was_pressed) {
        printf("pressed, position: %ld\n", (long)encoder_get_position(0));
    }
    was_pressed = pressed;
}
