// The steps of main(), one function each. main.c explains what the on-board
// buttons and LEDs mean.
#ifndef MAIN_HELPERS_H
#define MAIN_HELPERS_H

#include <stdbool.h>

// --- start-up ---
void init_board(void);        // clocks, pins and the debug UART. Call first.
void start_boot_beep(void);   // 1 kHz tone for 1 s; call after init_audio()

// --- every 1 ms ---
void wait_for_next_millisecond(void);

// Call exactly once per millisecond. True on every 50th call: time to run the
// status functions below.
bool status_tick_is_due(void);

// --- every status tick (50 ms), in this order ---
void end_boot_beep_when_done(void);   // beep over -> passthrough
void handle_test_mode_button(void);   // BUTTON1: passthrough -> sine -> silence
void handle_effect_button(void);      // BUTTON2: next effect
void update_meters(void);             // input bias + output level, for the LEDs and debugger
void update_status_leds(void);        // LED1: test mode. LED2: input bias / output signal

#endif /* MAIN_HELPERS_H */
