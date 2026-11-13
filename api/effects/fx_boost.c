#include "fx_boost.h"

#define BOOST_MIN_DB 0.0f
#define BOOST_MAX_DB 20.0f

void fx_boost_init(fx_boost_t *state, float sample_rate)
{
    /* x1 (0 dB): no change until the knob is set */
    fx_smoothed_init(&state->gain, sample_rate, 1.0f);
}

void fx_boost_set_gain(fx_boost_t *state, float knob)
{
    state->gain.target = fx_knob_to_gain(knob, BOOST_MIN_DB, BOOST_MAX_DB);
}

void fx_boost_process(fx_boost_t *state, const float *input, float *output,
                      uint32_t num_samples)
{
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        float gain = fx_smoothed_next(&state->gain);
        output[sample_index] = input[sample_index] * gain;
    }
}
