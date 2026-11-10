#include "fx_overdrive.h"

#define OVERDRIVE_DRIVE_MIN_DB 0.0f
#define OVERDRIVE_DRIVE_MAX_DB 40.0f
#define OVERDRIVE_LEVEL_MIN_DB -20.0f
#define OVERDRIVE_LEVEL_MAX_DB 0.0f

void fx_overdrive_init(fx_overdrive_t *state, float sample_rate)
{
    fx_smoothed_init(&state->drive_gain, sample_rate, 1.0f);
    fx_smoothed_init(&state->level_gain, sample_rate, 1.0f);

    /* Start with both knobs at noon: +20 dB drive, -10 dB level. On a typical
     * guitar signal that's a medium crunch at roughly the input volume.
     * Skip the glide so the effect doesn't fade in at power-on. */
    fx_overdrive_set_drive(state, 0.5f);
    fx_overdrive_set_level(state, 0.5f);
    fx_smoothed_skip_to_target(&state->drive_gain);
    fx_smoothed_skip_to_target(&state->level_gain);
}

void fx_overdrive_set_drive(fx_overdrive_t *state, float knob)
{
    float decibels = fx_knob_to_range(knob, OVERDRIVE_DRIVE_MIN_DB, OVERDRIVE_DRIVE_MAX_DB);
    state->drive_gain.target = fx_db_to_gain(decibels);
}

void fx_overdrive_set_level(fx_overdrive_t *state, float knob)
{
    float decibels = fx_knob_to_range(knob, OVERDRIVE_LEVEL_MIN_DB, OVERDRIVE_LEVEL_MAX_DB);
    state->level_gain.target = fx_db_to_gain(decibels);
}

void fx_overdrive_process(fx_overdrive_t *state, const float *input, float *output,
                          uint32_t num_samples)
{
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        float drive = fx_smoothed_next(&state->drive_gain);
        float level = fx_smoothed_next(&state->level_gain);
        float clipped = fx_soft_clip(input[sample_index] * drive);
        output[sample_index] = clipped * level;
    }
}
