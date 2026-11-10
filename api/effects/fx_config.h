#ifndef FX_CONFIG_H
#define FX_CONFIG_H

/*
 * Numbers that more than one part of the pedal must agree on.
 *
 * Numbers only one effect cares about (like BOOST_MAX_DB) stay in that
 * effect's .c file instead.
 */

/* Samples per audio block. The firmware's DMA ping-pong halves and the Mac
 * harness both process blocks of this size. */
#define FX_BLOCK_SIZE 32

/* Sample rate the firmware runs at. Effects don't read this directly: they get
 * the real rate passed into init(), so the harness can run any WAV file. */
#define FX_SAMPLE_RATE 48000

/* How fast knob changes glide (see fx_smoothed_t in fx_common.h). 10 Hz gets
 * most of the way to a new value in about 50 ms: quick enough to feel
 * instant, slow enough not to click. */
#define FX_SMOOTHING_HZ 10.0f

#endif
