#ifndef FX_CONFIG_H
#define FX_CONFIG_H

/*
 * Numbers every effect must agree on. Numbers only one effect cares about
 * (like BOOST_MAX_DB) stay in that effect's .c file.
 *
 * There is no sample rate here on purpose: each effect gets the real rate
 * passed into init() (AUDIO_SAMPLE_RATE_HZ in api/board.h).
 */

/* How fast knob changes glide (see fx_smoothed_t in fx_common.h). 10 Hz gets
 * most of the way to a new value in about 50 ms: quick enough to feel
 * instant, slow enough not to click. */
#define FX_SMOOTHING_HZ 10.0f

#endif
