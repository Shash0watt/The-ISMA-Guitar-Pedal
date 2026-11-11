#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// === Pin Setup ===
// P14.0  audio in  (VADC G0CH0)        see api/adc
// P14.8  audio out (DAC0)              see api/dac
// P14.9  reserved  (DAC1, future stereo)
// P5.9   LED1 (BSP: CYBSP_USER_LED)   heartbeat, 1 Hz while audio runs
// P5.8   LED2 (BSP: CYBSP_USER_LED2)  on while the DAC output carries signal


// === Rotary Encoder ===
// A timer polls the rotary encoder for a change 
// MAKE SURE IT"S LOWER PRIORITY HAN AUDIO

// Starts a timer that reads the rotary encoder's changes
void init_rotary_encoder();


// === Audio (api/audio) ===
// Sample engine. Owns SysTick, running at the sample rate with NVIC priority 0
// (highest). Every other timer/interrupt must use a LOWER priority (a larger number).
// Each tick: read ADC -> effect -> write DAC. One sample of latency.
// Samples passed to effects are centered on 0: -2048..2047 (12-bit).

// 96 kHz: keeps the DAC's stair-step treble loss and images far above the
// audio band. Set back to 48000U to halve the ISR load.
#define AUDIO_SAMPLE_RATE_HZ 96000U

// Effect callback: takes one input sample, returns one output sample (both
// centered on 0). Runs inside the sample ISR: keep it short (< ~1200 cycles
// at 96 kHz, < ~2500 at 48 kHz) and never block. The output is clipped to 12 bits for you.
typedef int32_t (*audio_effect_t)(int32_t sample);

typedef enum {
    AUDIO_TEST_NONE = 0,   // normal: ADC -> effect -> DAC
    AUDIO_TEST_MIDSCALE,   // DAC holds 2048 (~1.4 V DC on P14.8)
    AUDIO_TEST_SINE_1K,    // DAC plays a 1 kHz sine (2048 +/- 1000), any sample rate
} audio_test_mode_t;

// Inits ADC + DAC and starts the sample interrupt. Call once, after cybsp_init().
void init_audio(uint32_t sample_rate_hz);

// Install an effect. NULL = straight passthrough (the default).
void audio_set_effect(audio_effect_t effect);

// Bring-up test modes. The ADC is still sampled in every mode.
void audio_set_test_mode(audio_test_mode_t mode);

// Samples processed since init (wraps). Increments by sample_rate each second.
uint32_t audio_sample_count(void);

// Peak DAC deviation from mid-scale, |out - 2048| (0..2048), since the last
// call. Reading clears it. Silence reads near 0; the 1 kHz test reads ~1000.
uint16_t audio_out_peak(void);

// Average ADC input (0..4095) since the last call. Reading clears it.
// With the input silent this is the bias level: ~2048 if the divider is right.
uint16_t audio_in_mean(void);


// === ADC (api/adc) ===
// VADC group 0, channel 0 on P14.0. 12-bit, 1.65 V bias -> ~2048 on silence.
// Each adc_start_conversion() runs ADC_OVERSAMPLE conversions (~1 us each)
// and adc_read() returns their average. 4x averaging = +6 dB SNR. Set to 1 to undo.
#define ADC_OVERSAMPLE 4   // 1, 2 or 4

void init_adc(void);
void adc_start_conversion(void);
uint16_t adc_read(void);               // latest result, 0..4095


// === DAC (api/dac) ===
// DAC0 on P14.8. Single-value mode: a write updates the pin immediately.
// 0..4095 -> ~0.3..2.5 V.

void init_dac(void);
void dac_write(uint16_t code);         // 0..4095 (upper bits ignored)


#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
