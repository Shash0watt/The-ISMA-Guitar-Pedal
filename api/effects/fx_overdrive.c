#include "fx_overdrive.h"
#include "fx_common.h"

#define OVERDRIVE_DRIVE_MIN_DB 0.0f
#define OVERDRIVE_DRIVE_MAX_DB 40.0f
#define OVERDRIVE_LEVEL_MIN_DB -20.0f
#define OVERDRIVE_LEVEL_MAX_DB 0.0f

void fx_overdrive_init(fx_overdrive_t *state, float sample_rate)
{
    (void)sample_rate; /* not needed until we add a tone filter */

    /* Start with both knobs at noon: +20 dB drive, -10 dB level. On a typical
     * guitar signal that's a medium crunch at roughly the input volume. */
    fx_overdrive_set_drive(state, 0.5f);
    fx_overdrive_set_level(state, 0.5f);
}

void fx_overdrive_set_drive(fx_overdrive_t *state, float knob)
{
    float decibels = fx_knob_to_range(knob, OVERDRIVE_DRIVE_MIN_DB, OVERDRIVE_DRIVE_MAX_DB);
    state->drive_gain = fx_db_to_gain(decibels);
}

void fx_overdrive_set_level(fx_overdrive_t *state, float knob)
{
    float decibels = fx_knob_to_range(knob, OVERDRIVE_LEVEL_MIN_DB, OVERDRIVE_LEVEL_MAX_DB);
    state->level_gain = fx_db_to_gain(decibels);
}

void fx_overdrive_process(fx_overdrive_t *state, const float *input, float *output,
                          uint32_t num_samples)
{
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        float driven = input[sample_index] * state->drive_gain;
        float clipped = fx_soft_clip(driven);
        output[sample_index] = clipped * state->level_gain;
    }
}
