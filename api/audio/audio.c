// Audio sample engine: SysTick interrupt at the sample rate, ADC -> effect -> DAC.
#include "../board.h"
#include "cybsp.h"

#define AUDIO_MID    2048                   // 12-bit mid-scale = silence
#define DAC_MAX_CODE 4095

#define SINE_TABLE_LENGTH      48U
#define SINE_TEST_FREQUENCY_HZ 1000U
// The sine phase is a table index in 16.16 fixed point; it wraps after one cycle.
#define SINE_PHASE_WRAP ((uint32_t)SINE_TABLE_LENGTH << 16)

// Watch these in the debugger (Live Watch) during bring-up.
// audio_in_last reads ~2048 on silence, ~1500..2600 with 1 Vrms music.
volatile uint16_t audio_in_last;
volatile uint16_t audio_out_last;

static volatile uint32_t sample_count;
static volatile uint32_t input_sum, input_count;            // for audio_in_mean()
static volatile uint16_t output_peak;                       // for audio_out_peak()
static volatile audio_effect_t current_effect;              // NULL = passthrough
static volatile audio_test_mode_t test_mode = AUDIO_TEST_NONE;
static uint32_t sine_phase_step;                            // table entries per sample, 16.16

// One sine cycle, 2048 +/- 1000 counts.
static const uint16_t sine_table[SINE_TABLE_LENGTH] = {
    2048, 2179, 2307, 2431, 2548, 2657, 2755, 2841, 2914, 2972, 3014, 3039,
    3048, 3039, 3014, 2972, 2914, 2841, 2755, 2657, 2548, 2431, 2307, 2179,
    2048, 1917, 1789, 1665, 1548, 1439, 1341, 1255, 1182, 1124, 1082, 1057,
    1048, 1057, 1082, 1124, 1182, 1255, 1341, 1439, 1548, 1665, 1789, 1917,
};

// SysTick counts core clocks: 144 MHz / 96 kHz = 1500 per sample.
// Priority 0 is the highest, so nothing delays a sample.
static void start_sample_interrupt(uint32_t sample_rate_hz)
{
    SysTick_Config(SystemCoreClock / sample_rate_hz);
    NVIC_SetPriority(SysTick_IRQn, 0);
}

void init_audio(uint32_t sample_rate_hz)
{
    init_dac();
    init_adc();

    // Table entries to advance per sample for a 1 kHz tone (0.5 at 96 kHz).
    sine_phase_step = (uint32_t)(((uint64_t)SINE_TABLE_LENGTH * SINE_TEST_FREQUENCY_HZ << 16)
                                 / sample_rate_hz);

    start_sample_interrupt(sample_rate_hz);
}

void audio_set_effect(audio_effect_t effect)     { current_effect = effect; }
void audio_set_test_mode(audio_test_mode_t mode) { test_mode = mode; }
uint32_t audio_sample_count(void)                { return sample_count; }

// The two meter reads below turn interrupts off for a few cycles so the sample
// interrupt cannot change a meter between reading it and clearing it.
uint16_t audio_in_mean(void)
{
    __disable_irq();
    uint32_t sum = input_sum, count = input_count;
    input_sum = 0; input_count = 0;
    __enable_irq();
    return count ? (uint16_t)(sum / count) : 0;
}

uint16_t audio_out_peak(void)
{
    __disable_irq();
    uint16_t peak = output_peak;
    output_peak = 0;
    __enable_irq();
    return peak;
}

// --- Steps of the sample interrupt ---
// __STATIC_FORCEINLINE (CMSIS) is `static inline` that the compiler must obey.
// The Debug build (-Og) leaves plain `static inline` as real calls, which
// would cost cycles on every sample.

__STATIC_FORCEINLINE uint16_t next_test_sine_sample(void)
{
    static uint32_t phase;
    uint16_t sample = sine_table[phase >> 16];
    phase += sine_phase_step;
    if (phase >= SINE_PHASE_WRAP) { phase -= SINE_PHASE_WRAP; }
    return sample;
}

__STATIC_FORCEINLINE uint16_t clamp_to_dac_range(int32_t sample)
{
    if (sample < 0)            { sample = 0; }
    if (sample > DAC_MAX_CODE) { sample = DAC_MAX_CODE; }
    return (uint16_t)sample;
}

// Effects work on samples centered on 0, so remove the mid-scale offset going
// in and add it back coming out.
__STATIC_FORCEINLINE uint16_t run_effect(uint16_t input_sample)
{
    int32_t sample = (int32_t)input_sample - AUDIO_MID;
    audio_effect_t effect = current_effect;
    if (effect) { sample = effect(sample); }
    return clamp_to_dac_range(sample + AUDIO_MID);
}

__STATIC_FORCEINLINE uint16_t next_output_sample(uint16_t input_sample)
{
    if (test_mode == AUDIO_TEST_MIDSCALE) { return AUDIO_MID; }
    if (test_mode == AUDIO_TEST_SINE_1K)  { return next_test_sine_sample(); }
    return run_effect(input_sample);
}

// Peak = largest output distance from mid-scale. Mean = input sum / input count.
__STATIC_FORCEINLINE void update_meters(uint16_t input_sample, uint16_t output_sample)
{
    uint16_t output_deviation = (output_sample >= AUDIO_MID) ? (output_sample - AUDIO_MID)
                                                             : (AUDIO_MID - output_sample);
    if (output_deviation > output_peak) { output_peak = output_deviation; }
    input_sum += input_sample; input_count++;

    audio_in_last  = input_sample;
    audio_out_last = output_sample;
    sample_count++;
}

void SysTick_Handler(void)
{
    // This reads the conversion started on the previous tick (finished ~4 us
    // after it). Starting the next one first thing keeps the sample instant fixed.
    uint16_t input_sample = adc_read();
    adc_start_conversion();

    uint16_t output_sample = next_output_sample(input_sample);
    dac_write(output_sample);

    update_meters(input_sample, output_sample);
}
