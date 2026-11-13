// The pedal: runs one effect from api/effects on the live audio and connects
// the knobs to it.
//
//   effect      knob 0    knob 1
//   bypass      -         -
//   boost       gain      -
//   overdrive   drive     level
//
// boost_sample() and overdrive_sample() run inside the audio interrupt, once
// per sample (96,000 times a second), so they must only do fast math: no
// printf, no powf, nothing that waits. Everything else runs from the main
// loop. The two sides share only the effects' knob targets, and each target is
// a single float, so the interrupt never sees a half-written value.
#include "../board.h"
#include "../effects/fx_boost.h"
#include "../effects/fx_overdrive.h"
#include <stdio.h>

typedef enum {
    EFFECT_BYPASS = 0,
    EFFECT_BOOST,
    EFFECT_OVERDRIVE,
    EFFECT_COUNT
} effect_t;

static const char *const effect_names[EFFECT_COUNT] = {
    [EFFECT_BYPASS]    = "bypass",
    [EFFECT_BOOST]     = "boost",
    [EFFECT_OVERDRIVE] = "overdrive",
};

// The audio engine's samples are 12-bit integers, -2048..2047. The effects
// work in float, -1..1.
#define SAMPLE_FULL_SCALE 2048.0f

static fx_boost_t boost_state;
static fx_overdrive_t overdrive_state;
static effect_t current_effect = EFFECT_BYPASS;

// Each effect's saved knob settings (0..1). Encoders have no end stops, so
// switching effects moves the knobs back to where that effect left them.
// The values here are the power-on settings.
static float knob_values[EFFECT_COUNT][NUM_ENCODERS] = {
    [EFFECT_BOOST]     = { 0.0f },        // gain 0 dB: sounds like bypass until turned up
    [EFFECT_OVERDRIVE] = { 0.5f, 0.5f },  // drive +20 dB, level -10 dB: medium crunch
};


// --- Audio interrupt side ---------------------------------------------------

static inline float sample_to_float(int32_t sample)
{
    return (float)sample / SAMPLE_FULL_SCALE;
}

static inline int32_t float_to_sample(float value)
{
    return (int32_t)(value * SAMPLE_FULL_SCALE);
}

// The engine hands over one sample at a time, so each effect processes a
// block of one sample. The engine clips the result to 12 bits.
static int32_t boost_sample(int32_t sample)
{
    float input = sample_to_float(sample);
    float output;
    fx_boost_process(&boost_state, &input, &output, 1);
    return float_to_sample(output);
}

static int32_t overdrive_sample(int32_t sample)
{
    float input = sample_to_float(sample);
    float output;
    fx_overdrive_process(&overdrive_state, &input, &output, 1);
    return float_to_sample(output);
}


// --- Main loop side ---------------------------------------------------------

// The knob table at the top of the file, written as code.
static void send_knob_to_effect(effect_t effect, int knob_index, float knob)
{
    switch (effect) {
    case EFFECT_BOOST:
        if (knob_index == 0) { fx_boost_set_gain(&boost_state, knob); }
        break;
    case EFFECT_OVERDRIVE:
        if (knob_index == 0) { fx_overdrive_set_drive(&overdrive_state, knob); }
        if (knob_index == 1) { fx_overdrive_set_level(&overdrive_state, knob); }
        break;
    default:
        break;  // bypass has no knobs
    }
}

// Moves the knobs to where this effect left them.
static void restore_saved_knobs(effect_t effect)
{
    for (int knob_index = 0; knob_index < NUM_ENCODERS; knob_index++) {
        float knob = knob_values[effect][knob_index];
        encoder_set_knob(knob_index, knob);
        send_knob_to_effect(effect, knob_index, knob);
    }
}

// Tells the audio engine which function to run on every sample.
static void route_audio_to(effect_t effect)
{
    switch (effect) {
    case EFFECT_BOOST:     audio_set_effect(boost_sample);     break;
    case EFFECT_OVERDRIVE: audio_set_effect(overdrive_sample); break;
    default:               audio_set_effect(NULL);             break;  // straight through
    }
}

static void select_effect(effect_t effect)
{
    current_effect = effect;
    restore_saved_knobs(effect);
    route_audio_to(effect);
    printf("effect: %s\n", effect_names[effect]);
}

void init_pedal(void)
{
    fx_boost_init(&boost_state, (float)AUDIO_SAMPLE_RATE_HZ);
    fx_overdrive_init(&overdrive_state, (float)AUDIO_SAMPLE_RATE_HZ);
    select_effect(EFFECT_BYPASS);
}

void pedal_next_effect(void)
{
    select_effect((effect_t)((current_effect + 1) % EFFECT_COUNT));
}

// Call every 1 ms. When a knob has moved, saves the new value and sends it to
// the current effect. Comparing floats with != is safe here: a knob only ever
// holds whole clicks, so an unmoved knob gives exactly the same number.
void poll_pedal(void)
{
    for (int knob_index = 0; knob_index < NUM_ENCODERS; knob_index++) {
        float knob = encoder_get_knob(knob_index);
        if (knob != knob_values[current_effect][knob_index]) {
            knob_values[current_effect][knob_index] = knob;
            send_knob_to_effect(current_effect, knob_index, knob);
        }
    }
}
