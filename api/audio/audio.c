// Audio sample engine: SysTick ISR at the sample rate, ADC -> effect -> DAC.
// See api/README.md.
#include "../board.h"
#include "cybsp.h"

#define AUDIO_MID 2048

// Debug: watch these in the debugger (Live Watch) during bring-up.
//   audio_in_last  ~2048 on silence, ~1500..2600 with 1 Vrms music.
volatile uint16_t audio_in_last;
volatile uint16_t audio_out_last;

static volatile uint32_t sample_count;
static volatile uint16_t out_peak;                         // max |out - 2048| since last read
static volatile audio_effect_t current_effect;              // NULL = passthrough
static volatile audio_test_mode_t test_mode = AUDIO_TEST_NONE;
static uint32_t sine_step;                                  // table step per sample, 16.16 fixed point

// One sine cycle, 48 points, 2048 +/- 1000 counts. Stepped to give 1 kHz.
static const uint16_t sine48[48] = {
    2048, 2179, 2307, 2431, 2548, 2657, 2755, 2841, 2914, 2972, 3014, 3039,
    3048, 3039, 3014, 2972, 2914, 2841, 2755, 2657, 2548, 2431, 2307, 2179,
    2048, 1917, 1789, 1665, 1548, 1439, 1341, 1255, 1182, 1124, 1082, 1057,
    1048, 1057, 1082, 1124, 1182, 1255, 1341, 1439, 1548, 1665, 1789, 1917,
};

void init_audio(uint32_t sample_rate_hz)
{
    init_dac();
    init_adc();

    // 1 kHz: advance 48 * 1000 / rate table entries per sample (0.5 at 96 kHz)
    sine_step = (uint32_t)(((uint64_t)48U * 1000U << 16) / sample_rate_hz);

    // 144 MHz / 96 kHz = 1500 core clocks per sample. Highest priority.
    SysTick_Config(SystemCoreClock / sample_rate_hz);
    NVIC_SetPriority(SysTick_IRQn, 0);
}

void audio_set_effect(audio_effect_t effect)    { current_effect = effect; }
void audio_set_test_mode(audio_test_mode_t mode) { test_mode = mode; }
uint32_t audio_sample_count(void)                { return sample_count; }

uint16_t audio_out_peak(void)
{
    __disable_irq();                // a few cycles; keeps read + clear atomic
    uint16_t p = out_peak;
    out_peak = 0;
    __enable_irq();
    return p;
}

void SysTick_Handler(void)
{
    // Result of the conversions started last tick (done ~4 us after it),
    // then start the next one right away so the sample instant stays fixed.
    uint16_t in = adc_read();
    adc_start_conversion();

    uint16_t out;
    if (test_mode == AUDIO_TEST_MIDSCALE)
    {
        out = AUDIO_MID;
    }
    else if (test_mode == AUDIO_TEST_SINE_1K)
    {
        static uint32_t phase;                  // 16.16 index into sine48
        out = sine48[phase >> 16];
        phase += sine_step;
        if (phase >= (48UL << 16)) { phase -= (48UL << 16); }
    }
    else
    {
        int32_t y = (int32_t)in - AUDIO_MID;
        audio_effect_t fx = current_effect;
        if (fx) { y = fx(y); }
        y += AUDIO_MID;
        if (y < 0)    { y = 0; }
        if (y > 4095) { y = 4095; }
        out = (uint16_t)y;
    }

    dac_write(out);

    uint16_t dev = (out >= AUDIO_MID) ? (out - AUDIO_MID) : (AUDIO_MID - out);
    if (dev > out_peak) { out_peak = dev; }

    audio_in_last  = in;
    audio_out_last = out;
    sample_count++;
}
