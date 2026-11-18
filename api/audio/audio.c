// Audio sample engine: one interrupt per sample, input -> effect -> output.
//   AUDIO_USE_I2S 1: the I2S receive interrupt. PCM1808 in, PCM5102 out.
//   AUDIO_USE_I2S 0: SysTick at the sample rate. On-chip ADC in, DAC out.
// Both call process_sample(), which works on samples centered on 0.
#include "../board.h"
#include "cybsp.h"
#include <math.h>

#define SINE_TABLE_LENGTH      48U
#define SINE_TEST_FREQUENCY_HZ 1000U
// The sine phase is a table index in 16.16 fixed point; it wraps after one cycle.
#define SINE_PHASE_WRAP ((uint32_t)SINE_TABLE_LENGTH << 16)

#if AUDIO_USE_I2S
// -20 dBFS: about 0.2 Vrms out of the PCM5102. Full scale (2.1 Vrms) would be
// painful in headphones.
#define SINE_TEST_AMPLITUDE (AUDIO_FULL_SCALE / 10)
#else
// About -6 dBFS: 0.54 V peak on P14.8.
#define SINE_TEST_AMPLITUDE 1000
#endif

#define DAC_MID_CODE 2048    // on-chip only: 12-bit unsigned codes, mid-scale = silence

// Watch these in the debugger (Live Watch) during bring-up. Both are centered
// on 0: audio_in_last stays near 0 on silence and swings with music.
volatile int32_t audio_in_last;
volatile int32_t audio_out_last;

static volatile uint32_t sample_count;
static volatile int32_t input_sum;                          // for audio_in_mean()
static volatile uint32_t input_count;
static volatile uint32_t output_peak;                       // for audio_out_peak()
static volatile audio_effect_t current_effect;              // NULL = passthrough
static volatile audio_test_mode_t test_mode = AUDIO_TEST_NONE;
static uint32_t sine_phase_step;                            // table entries per sample, 16.16
static int16_t sine_table[SINE_TABLE_LENGTH];               // one cycle, centered on 0

static void fill_sine_table(void)
{
    for (uint32_t index = 0; index < SINE_TABLE_LENGTH; index++)
    {
        float angle = 6.2831853f * (float)index / (float)SINE_TABLE_LENGTH;
        sine_table[index] = (int16_t)lrintf((float)SINE_TEST_AMPLITUDE * sinf(angle));
    }
}

void init_audio(uint32_t sample_rate_hz)
{
    fill_sine_table();
    // Table entries to advance per sample for a 1 kHz tone (~1.02 at 46.875 kHz).
    sine_phase_step = (uint32_t)(((uint64_t)SINE_TABLE_LENGTH * SINE_TEST_FREQUENCY_HZ << 16)
                                 / sample_rate_hz);

#if AUDIO_USE_I2S
    init_i2s();                          // its interrupt is the sample clock
#else
    init_dac();
    init_adc();
    // SysTick counts core clocks: 144 MHz / 96 kHz = 1500 per sample.
    SysTick_Config(SystemCoreClock / sample_rate_hz);
    NVIC_SetPriority(SysTick_IRQn, 0);   // highest: nothing delays a sample
#endif
}

void audio_set_effect(audio_effect_t effect)     { current_effect = effect; }
void audio_set_test_mode(audio_test_mode_t mode) { test_mode = mode; }
uint32_t audio_sample_count(void)                { return sample_count; }

// The two meter reads below turn interrupts off for a few cycles so the sample
// interrupt cannot change a meter between reading it and clearing it.
int32_t audio_in_mean(void)
{
    __disable_irq();
    int32_t sum = input_sum;
    uint32_t count = input_count;
    input_sum = 0; input_count = 0;
    __enable_irq();
    return count ? sum / (int32_t)count : 0;
}

uint32_t audio_out_peak(void)
{
    __disable_irq();
    uint32_t peak = output_peak;
    output_peak = 0;
    __enable_irq();
    return peak;
}

// --- Steps of the sample interrupt ---
// __STATIC_FORCEINLINE (CMSIS) is `static inline` that the compiler must obey.
// The Debug build (-Og) leaves plain `static inline` as real calls, which
// would cost cycles on every sample.

__STATIC_FORCEINLINE int32_t next_test_sine_sample(void)
{
    static uint32_t phase;
    int32_t sample = sine_table[phase >> 16];
    phase += sine_phase_step;
    if (phase >= SINE_PHASE_WRAP) { phase -= SINE_PHASE_WRAP; }
    return sample;
}

__STATIC_FORCEINLINE int32_t clip(int32_t sample)
{
    if (sample < -AUDIO_FULL_SCALE)     { sample = -AUDIO_FULL_SCALE; }
    if (sample > AUDIO_FULL_SCALE - 1)  { sample = AUDIO_FULL_SCALE - 1; }
    return sample;
}

// Peak = largest output distance from 0. Mean = input sum / input count.
// input_sum can't overflow: the main loop reads it every 50 ms, which is at
// most 4800 samples of at most 32768 each.
__STATIC_FORCEINLINE void update_meters(int32_t input, int32_t output)
{
    uint32_t magnitude = (uint32_t)((output < 0) ? -output : output);
    if (magnitude > output_peak) { output_peak = magnitude; }
    input_sum += input; input_count++;

    audio_in_last  = input;
    audio_out_last = output;
    sample_count++;
}

// One sample: centered input in, centered output out (already clipped).
__STATIC_FORCEINLINE int32_t process_sample(int32_t input)
{
    int32_t output;
    if (test_mode == AUDIO_TEST_MIDSCALE)      { output = 0; }
    else if (test_mode == AUDIO_TEST_SINE_1K)  { output = next_test_sine_sample(); }
    else
    {
        audio_effect_t effect = current_effect;
        output = clip(effect ? effect(input) : input);
    }
    update_meters(input, output);
    return output;
}

#if AUDIO_USE_I2S

// Runs when the receive FIFO holds a whole frame. It loops in case a frame
// arrived while it was running. Mono: left and right are averaged going in,
// and the same sample goes to both sides coming out. The frame written now
// plays a few frames later (the transmit FIFO was primed with silence).
void USIC2_0_IRQHandler(void)   // USIC2 ch1 receive FIFO, see api/i2s/i2s.c
{
    int16_t left, right;
    while (i2s_read_frame(&left, &right))
    {
        int16_t output = (int16_t)process_sample(((int32_t)left + right) / 2);
        i2s_write_frame(output, output);
    }
}

#else

void SysTick_Handler(void)
{
    // This reads the conversion started on the previous tick (finished ~4 us
    // after it). Starting the next one first thing keeps the sample instant fixed.
    int32_t input = (int32_t)adc_read() - DAC_MID_CODE;
    adc_start_conversion();

    dac_write((uint16_t)(process_sample(input) + DAC_MID_CODE));
}

#endif
