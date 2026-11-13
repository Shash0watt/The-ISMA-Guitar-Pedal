#ifndef FX_COMMON_H
#define FX_COMMON_H

/*
 * Small helpers shared by every effect.
 *
 * They are "static inline" so the compiler can paste them straight into the
 * process() loops, which run once per sample: no function-call overhead.
 *
 * Everything is float, never double: the XMC4700's FPU only does single
 * precision, so double math would run slowly in software.
 */

#include <math.h>

#include "fx_config.h"

#define FX_TWO_PI 6.28318530717959f

/* Keeps value between minimum and maximum. */
static inline float fx_clamp(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* Converts decibels to a multiplier: gain = 10^(decibels / 20).
 *   0 dB -> 1     +6 dB -> about 2     +20 dB -> 10     -20 dB -> 0.1
 * Uses powf, which is slow, so call it when a knob changes, not on every
 * sample. */
static inline float fx_db_to_gain(float decibels)
{
    return powf(10.0f, decibels / 20.0f);
}

/* Maps a 0..1 knob onto minimum..maximum in a straight line. The knob is
 * clamped first, so a bad reading can't push the result out of range. */
static inline float fx_knob_to_range(float knob, float minimum, float maximum)
{
    knob = fx_clamp(knob, 0.0f, 1.0f);
    return minimum + knob * (maximum - minimum);
}

/* Turns a 0..1 knob into a multiplier, with the knob sweeping evenly in dB
 * from minimum_db to maximum_db. Slow (powf): for knob setters only. */
static inline float fx_knob_to_gain(float knob, float minimum_db, float maximum_db)
{
    return fx_db_to_gain(fx_knob_to_range(knob, minimum_db, maximum_db));
}

/* Soft clipper: sample / (1 + |sample|).
 * Small signals pass nearly unchanged; big ones bend over smoothly toward
 * +-1 but never reach it, so the output can never clip the DAC.
 *   0.1 -> 0.09     1 -> 0.5     10 -> 0.91     100 -> 0.99 */
static inline float fx_soft_clip(float sample)
{
    return sample / (1.0f + fabsf(sample));
}

/*
 * A value that glides toward its target instead of jumping, so turning a knob
 * doesn't click.
 *
 *   setter (knob moved):     smoothed.target = new value
 *   process (every sample):  value = fx_smoothed_next(&smoothed)
 *
 * Each sample, current moves a fixed fraction (coefficient) of the remaining
 * distance to target: a very slow low-pass filter on the knob value.
 */
typedef struct {
    float target;      /* where the knob says the value should be */
    float current;     /* the value actually used on the audio right now */
    float coefficient; /* fraction of the gap closed per sample (0..1) */
} fx_smoothed_t;

/* Sets the glide speed from FX_SMOOTHING_HZ and starts at initial_value.
 *   coefficient = 1 - e^(-2 * pi * FX_SMOOTHING_HZ / sample_rate)
 * Uses expf, so call it from init(), not per sample. */
static inline void fx_smoothed_init(fx_smoothed_t *smoothed, float sample_rate,
                                    float initial_value)
{
    smoothed->coefficient = 1.0f - expf(-FX_TWO_PI * FX_SMOOTHING_HZ / sample_rate);
    smoothed->target = initial_value;
    smoothed->current = initial_value;
}

/* Moves current one sample's worth toward target and returns it. */
static inline float fx_smoothed_next(fx_smoothed_t *smoothed)
{
    smoothed->current += smoothed->coefficient * (smoothed->target - smoothed->current);
    return smoothed->current;
}

/* Jumps straight to target, so an effect doesn't fade in at power-on. */
static inline void fx_smoothed_skip_to_target(fx_smoothed_t *smoothed)
{
    smoothed->current = smoothed->target;
}

#endif
