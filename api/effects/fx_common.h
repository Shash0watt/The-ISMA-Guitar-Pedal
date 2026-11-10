#ifndef FX_COMMON_H
#define FX_COMMON_H

/*
 * Small helpers shared by every effect.
 *
 * They are "static inline": each file that includes this header gets its own
 * copy, which the compiler can paste straight into the calling code. No
 * function-call overhead, which matters inside process() loops that run
 * 48,000 times a second.
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

/* Converts decibels to a multiplier.
 *
 *   decibels = 20 * log10(gain)      definition of dB for amplitude
 *   gain     = 10^(decibels / 20)    solved for gain (what this returns)
 *
 *   0 dB -> 1     +6 dB -> about 2     +20 dB -> 10     -20 dB -> 0.1
 *
 * Uses powf, which is slow-ish, so call it when a knob changes, not on every
 * sample. */
static inline float fx_db_to_gain(float decibels)
{
    return powf(10.0f, decibels / 20.0f);
}

/* Maps a 0..1 knob onto minimum..maximum in a straight line. The knob is
 * clamped first, so a noisy pot reading can't push the result out of range.
 *   fx_knob_to_range(knob, 0.0f, 20.0f)    boost amount in dB
 *   fx_knob_to_range(knob, -20.0f, 0.0f)   output level in dB */
static inline float fx_knob_to_range(float knob, float minimum, float maximum)
{
    knob = fx_clamp(knob, 0.0f, 1.0f);
    return minimum + knob * (maximum - minimum);
}

/* Soft clipper: sample / (1 + |sample|).
 * Small signals pass through nearly unchanged; big ones bend over smoothly
 * toward +-1 but never reach it, so the output can never clip the DAC.
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
 * distance to target: big steps at first, then gently settling. It's a very
 * slow low-pass filter on the knob value.
 */
typedef struct {
    float target;      /* where the knob says the value should be */
    float current;     /* the value actually used on the audio right now */
    float coefficient; /* fraction of the gap closed per sample (0..1) */
} fx_smoothed_t;

/* Sets up the glide speed from FX_SMOOTHING_HZ and starts at initial_value
 * (no glide at startup). Uses expf, so call it from init(), not per sample.
 *
 *   coefficient = 1 - e^(-2 * pi * FX_SMOOTHING_HZ / sample_rate) */
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

/* Jumps straight to target with no glide. For init() after setting default
 * knob values, so the effect doesn't start with a fade. */
static inline void fx_smoothed_skip_to_target(fx_smoothed_t *smoothed)
{
    smoothed->current = smoothed->target;
}

#endif
