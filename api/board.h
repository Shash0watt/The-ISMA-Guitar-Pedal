#ifndef BOARD_H
#define BOARD_H

#include "xmc_gpio.h"
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

// === Pin Setup ===

// === Digital inputs ===
// A interrupt every 1ms caused by a period match with SystemCoreClockUpdate
// will poll the inputs

// --- system tick ---
void init_systick();
void SysTick_Handler(); // UPDATE THIS WHENEVER A NEW INPUT IS ADDED

// --- rotary encoder ---
#define NUM_ENCODERS 4// must match the number of entries in encoders[] (rotary_encoder.c)
#define ENCODER_MIN 0            // lowest position an encoder can reach
#define ENCODER_MAX 100          // highest position an encoder can reach
extern void poll_encoders(void); // poll every encoder (call from SysTick)
extern int32_t encoder_get_position(int index);
extern uint8_t encoder_is_pressed(int index);

// --- buttons ---
#define NUM_BUTTONS 4 // must match the number of entries in buttons[] (buttons.c)
extern void poll_buttons(void); // poll every button (call from SysTick)
extern uint8_t button_is_pressed(int index); // 1 while button[index] is held down

// === DAC ===

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
