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

#endif
