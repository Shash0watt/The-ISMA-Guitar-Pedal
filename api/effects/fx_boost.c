#include "fx_boost.h"

#define BOOST_MIN_DB 0.0f
#define BOOST_MAX_DB 20.0f

void fx_boost_init(fx_boost_t *state, float sample_rate)
{
    /* 0 dB (x1): no change until the knob is set */
    fx_smoothed_init(&state->gain_mult, sample_rate, 1.0f);
}

void fx_boost_set_gain(fx_boost_t *state, float knob)
{
    float decibels = fx_knob_to_range(knob, BOOST_MIN_DB, BOOST_MAX_DB);
    state->gain_mult.target = fx_db_to_gain(decibels);
}

void fx_boost_process(fx_boost_t *state, const float *input, float *output,
                      uint32_t num_samples)
{
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        float gain = fx_smoothed_next(&state->gain_mult);
        output[sample_index] = input[sample_index] * gain;
    }
}
