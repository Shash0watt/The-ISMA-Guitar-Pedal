#include "../board.h"

// rotary encoder struct (private to this file; other files use the functions in board.h)
typedef struct {
    XMC_GPIO_PORT_t *port;
    uint8_t pin_a, pin_b, button;
    uint8_t prev;               // last AB state (2 bits)
    int8_t  accum;              // sub-steps since last detent
    volatile int32_t position;  // detent count, can be read by the rest of the code
    volatile uint8_t is_pressed; // 1 while the button is held down, can be read by the rest of the code
} encoder_t;

// all encoders on the board (is_pressed starts at 0 = released)
static encoder_t encoders[NUM_ENCODERS] = {
    { .port = XMC_GPIO_PORT0, .pin_a = 0, .pin_b = 1,  .button = 2  },  // encoder 0: P0.0-P0.2
    { .port = XMC_GPIO_PORT0, .pin_a = 3, .pin_b = 4,  .button = 5  },  // encoder 1: P0.3-P0.5
    { .port = XMC_GPIO_PORT0, .pin_a = 6, .pin_b = 7,  .button = 8  },  // encoder 2: P0.6-P0.8
    { .port = XMC_GPIO_PORT0, .pin_a = 9, .pin_b = 10, .button = 11 },  // encoder 3: P0.9-P0.11
};


static void poll_encoder(encoder_t *encoder) {
    // the decoder sees either A change then B change or B change then A
    // if we sample too slow it will look like both values changes at once
    // Standard incremental encoders typically produce 12 to 24 pulses 
    // (detents) per full rotation. We are the shaft by hand.
    // sample rate >= margin x detents per revolution x state changes per 
    // detent x max revolutions per second 
    // ideally test this & calculate.. but google says that 1kHz is good enough

    // button.. pull-up input, so released = 1 and pressed = 0 -> invert to get is_pressed.
    // Must happen BEFORE the early return below, or it would only update while turning.
    // No debounce yet: if is_pressed flickers during one press, it's contact bounce.
    encoder->is_pressed = (XMC_GPIO_GetInput(encoder->port, encoder->button) == 0);

    // read encoder the encoder.. pin A will be bit 1, and pin B will be bit 0
    uint8_t curr = (XMC_GPIO_GetInput(encoder->port, encoder->pin_a) << 1) |
                    XMC_GPIO_GetInput(encoder->port, encoder->pin_b);

    if (curr == encoder->prev) return; // nothing moved

    // A valid step changes exactly one bit
    // case 1 ) Both bits changed then (prev ^ curr == 0b11)
    // we ended up missing a sample so the direction is unknown and we should ignore it
    // case 2) For a valid step, 
    //  prevA XOR currB == 0 then +1 (CW), == 1 then -1 (CCW)
    //   CW  sequence: 00 -> 10 -> 11 -> 01 -> 00   (B matches old A)
    //   CCW sequence: 00 -> 01 -> 11 -> 10 -> 00   (B is opposite of old A)
    uint8_t changed = encoder->prev ^ curr;
    if (changed != 0x3) {
        uint8_t prev_a = (encoder->prev >> 1) & 1;
        uint8_t curr_b = curr & 1;
        encoder->accum += (prev_a ^ curr_b) ? -1 : 1;
    }
    encoder->prev = curr;

    // detent logic
    // the encoder rests at 11 in each detent, increment only when we land 
    // back in the 'detent'
    // position is clamped to [ENCODER_MIN, ENCODER_MAX]: turning past an end does nothing
    if (curr == 0x3) {
        if      (encoder->accum >=  2 && encoder->position < ENCODER_MAX) encoder->position++;
        else if (encoder->accum <= -2 && encoder->position > ENCODER_MIN) encoder->position--;
        encoder->accum = 0;
    }
}

// call every 1 ms from SysTick_Handler
void poll_encoders(void) {
    for (int i = 0; i < NUM_ENCODERS; i++) {
        poll_encoder(&encoders[i]);
    }
}

// read-only access for other hardware
int32_t encoder_get_position(int index) {
    return encoders[index].position;
}

uint8_t encoder_is_pressed(int index) {
    return encoders[index].is_pressed;
}