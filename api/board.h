#ifndef BOARD_H
#define BOARD_H

#include "xmc_gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// === Pins ===
// Every pin the pedal uses, named once here; the rest of the code only uses
// these names. Each name stands for a port and a pin number together (P0_2
// means "XMC_GPIO_PORT0, 2"), so it can be passed straight to an XMC_GPIO
// function or used to fill in a pin_t.

// Which converters carry the audio:
//   1  external PCM1808 ADC + PCM5102 DAC over I2S (pins below)
//   0  the XMC's own ADC (P14.0) and DAC (P14.8)
// Both circuits can stay wired; only the selected one runs.
#define AUDIO_USE_I2S 1

// On-chip audio. These two are fixed by the chip: P14.0 is the ADC input used
// (group 0, channel 0) and P14.8 is the output of DAC0.
#define AUDIO_INPUT_PIN         P14_0   // biased to 1.65 V
#define AUDIO_OUTPUT_PIN        P14_8

// I2S audio: USIC2 channel 1, all on port 3 (headers X1/X2). See api/i2s/i2s.c.
// Clear of the encoders, buttons, LEDs, Ethernet, microSD and debug UART.
#define I2S_MCLK_PIN            P3_4    // X1.1   12 MHz -> PCM1808 SCKI
#define I2S_BCK_PIN             P3_13   // X2.1   3 MHz  -> both BCK
#define I2S_LRCK_PIN            P3_0    // X1.14  46.875 kHz -> both LRCK
#define I2S_DATA_IN_PIN         P3_12   // X2.2   <- PCM1808 DOUT
#define I2S_DATA_OUT_PIN        P3_11   // X2.3   -> PCM5102 DIN

// KY-040 encoders: CLK and DT are the two turn signals. Encoder and button
// pins read low when active. Their pull-ups come from the board config
// (Device Configurator), so a pin moved off P0 needs a pull-up set there too.
#define ENCODER_0_CLK_PIN       P0_0
#define ENCODER_0_DT_PIN        P0_1
#define ENCODER_0_BUTTON_PIN    P0_2
#define ENCODER_1_CLK_PIN       P0_3
#define ENCODER_1_DT_PIN        P0_4
#define ENCODER_1_BUTTON_PIN    P0_5
#define ENCODER_2_CLK_PIN       P0_6
#define ENCODER_2_DT_PIN        P0_7
#define ENCODER_2_BUTTON_PIN    P0_8
#define ENCODER_3_CLK_PIN       P0_9
#define ENCODER_3_DT_PIN        P0_10
#define ENCODER_3_BUTTON_PIN    P0_11

#define BUTTON_0_PIN            P0_15
#define BUTTON_1_PIN            P0_14
#define BUTTON_2_PIN            P0_13
#define BUTTON_3_PIN            P0_12

// On the Relax Kit itself.
#define TEST_MODE_BUTTON_PIN    P15_13  // BUTTON1
#define NEXT_EFFECT_BUTTON_PIN  P15_12  // BUTTON2
#define TEST_MODE_LED_PIN       P5_9    // LED1
#define SIGNAL_LED_PIN          P5_8    // LED2: input health / signal present

// printf goes out the board's debug UART (P1.5 TX, P1.4 RX, 115200 baud),
// which the board config sets up.

// A pin stored in a table: fill it in with one of the names above.
typedef struct {
    XMC_GPIO_PORT_t *port;
    uint8_t number;
} pin_t;

// 1 when the pin is high, 0 when it is low.
static inline uint32_t read_pin(pin_t pin)
{
    return XMC_GPIO_GetInput(pin.port, pin.number);
}


// === Digital inputs (api/digital_inputs) ===
// Read from the main loop, not from an interrupt, so printf is safe there.

void poll_inputs(void);  // call every 1 ms; add any new input to it

// Each encoder is a knob that reads 0.0 (fully down) to 1.0 (fully up), like a
// pot. The effects take their knobs in that same 0..1 form, so nothing in
// between has to convert. (This replaced encoder_get_position() and its 0..100
// range, which every caller had to divide by 100.)
#define NUM_ENCODERS 4    // must match encoders[] in rotary_encoder.c
#define ENCODER_STEPS 40  // clicks from 0.0 to 1.0: two full turns of a 20-click KY-040
void poll_encoders(void);
float encoder_get_knob(int index);             // 0.0 .. 1.0
void encoder_set_knob(int index, float knob);  // move a knob, e.g. to an effect's saved setting
uint8_t encoder_is_pressed(int index);         // 1 while the knob is pushed in

#define NUM_BUTTONS 4     // must match buttons[] in buttons.c
void poll_buttons(void);
uint8_t button_is_pressed(int index);          // 1 while the button is held down


// === Audio engine (api/audio) ===
// An interrupt at the sample rate: read input -> effect -> write output.
// It runs at the highest priority (0); every other interrupt must use a lower
// priority (a larger number) or the audio will glitch.
//   I2S:     the I2S receive interrupt (USIC2_0_IRQn), once per frame.
//            The converters' clocks set the rate. SysTick is free.
//   on-chip: SysTick.
//
// Samples are signed and centered on 0, in the range
// -AUDIO_FULL_SCALE .. AUDIO_FULL_SCALE - 1, on both paths.
#if AUDIO_USE_I2S
// 144 MHz / 3072: fixed by the I2S clock plan (exact 48 kHz isn't reachable).
#define AUDIO_SAMPLE_RATE_HZ 46875U
#define AUDIO_FULL_SCALE     32768   // 16-bit samples
#else
// 96 kHz keeps the DAC's stair-step artifacts far above what we can hear.
// 48000U halves the processor load.
#define AUDIO_SAMPLE_RATE_HZ 96000U
#define AUDIO_FULL_SCALE     2048    // 12-bit samples
#endif

// An effect takes one sample and returns one sample, both centered on 0. It
// runs inside the audio interrupt, so it must be quick (under ~2500 CPU cycles
// at 46.875 kHz, ~1200 at 96 kHz) and never wait. The result is clipped for you.
typedef int32_t (*audio_effect_t)(int32_t sample);

typedef enum {
    AUDIO_TEST_NONE = 0,   // normal: input -> effect -> output
    AUDIO_TEST_MIDSCALE,   // output held at 0: silence
    AUDIO_TEST_SINE_1K,    // output plays a 1 kHz sine
} audio_test_mode_t;

void init_audio(uint32_t sample_rate_hz);          // call once, after cybsp_init()
void audio_set_effect(audio_effect_t effect);      // NULL = straight passthrough (the default)
void audio_set_test_mode(audio_test_mode_t mode);
uint32_t audio_sample_count(void);                 // samples since init (wraps around)

// Meters. Reading one clears it, so each call covers the time since the last.
uint32_t audio_out_peak(void);  // loudest output, 0..AUDIO_FULL_SCALE
int32_t audio_in_mean(void);    // average input; ~0 when the input bias is right


// === Pedal (api/pedal) ===
// Runs one effect from api/effects on the live audio and connects the knobs to
// it. Which knob does what is listed at the top of api/pedal/pedal.c.

void init_pedal(void);         // call once, after init_audio()
void poll_pedal(void);         // call every 1 ms: sends knob changes to the effect
void pedal_next_effect(void);  // bypass -> boost -> overdrive -> bypass ...


// === I2S (api/i2s) ===
// Used by the audio engine when AUDIO_USE_I2S is 1. Mono for now: the engine
// averages left and right in, and writes the same sample to both out.

void init_i2s(void);            // starts the clocks and the receive interrupt
// True when it has read a whole frame (left + right) from the receive FIFO.
bool i2s_read_frame(int16_t *left, int16_t *right);
void i2s_write_frame(int16_t left, int16_t right);


// === ADC (api/adc) ===
// 12-bit. Each sample is the average of ADC_OVERSAMPLE quick conversions;
// averaging 4 halves the noise.
#define ADC_OVERSAMPLE 4   // 1, 2 or 4

void init_adc(void);
void adc_start_conversion(void);
uint16_t adc_read(void);        // 0..4095, from the conversion started last


// === DAC (api/dac) ===
// A write changes the output pin immediately. 0..4095 -> about 0.3..2.5 V.

void init_dac(void);
void dac_write(uint16_t code);  // 0..4095

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
