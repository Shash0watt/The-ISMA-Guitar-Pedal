#ifndef FX_OVERDRIVE_H
#define FX_OVERDRIVE_H

/*
 * Overdrive: push the signal hard into a soft clipper, then turn it back down.
 *
 *   input -> x drive gain -> fx_soft_clip -> x level gain -> output
 *
 * Knobs (0..1, like a pot):
 *   drive: 0 .. +40 dB of gain before the clipper (more = more distortion)
 *   level: -20 .. 0 dB output volume (heavy drive gets loud, this tames it)
 */

#include <stdint.h>

#include "fx_common.h"

typedef struct {
    fx_smoothed_t drive_gain; /* multiplier before the clipper, glides on change */
    fx_smoothed_t level_gain; /* multiplier after the clipper, glides on change */
} fx_overdrive_t;

void fx_overdrive_init(fx_overdrive_t *state, float sample_rate);
void fx_overdrive_set_drive(fx_overdrive_t *state, float knob);
void fx_overdrive_set_level(fx_overdrive_t *state, float knob);
void fx_overdrive_process(fx_overdrive_t *state, const float *input, float *output,
                          uint32_t num_samples);

#endif
