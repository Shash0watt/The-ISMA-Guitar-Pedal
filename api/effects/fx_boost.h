#ifndef FX_BOOST_H
#define FX_BOOST_H

/*
 * Clean boost: multiplies the signal by a gain, no distortion.
 *
 * Knob (0..1, like a pot):
 *   gain: 0 .. +20 dB
 */

#include <stdint.h>

#include "fx_common.h"

typedef struct {
    fx_smoothed_t gain_mult; /* multiplier applied to every sample, glides on change */
} fx_boost_t;

void fx_boost_init(fx_boost_t *state, float sample_rate);
void fx_boost_set_gain(fx_boost_t *state, float knob);
void fx_boost_process(fx_boost_t *state, const float *input, float *output,
                      uint32_t num_samples);

#endif
