/* Minimal WAV file reading/writing for the Mac test harness. */
#ifndef WAV_H
#define WAV_H

#include <stdint.h>

typedef struct {
    float *samples;        /* mono, -1..1 */
    uint32_t num_samples;
    uint32_t sample_rate;
    uint16_t num_channels; /* channels in the original file, before mixdown */
} wav_t;

/* Reads 8/16/24/32-bit PCM or 32/64-bit float WAV, mixed down to mono.
 * Returns 0 on success, prints an error and returns -1 on failure. */
int wav_read_mono(const char *path, wav_t *wav);

/* Writes mono 32-bit float WAV. Returns 0 on success, -1 on failure. */
int wav_write_mono_f32(const char *path, const float *samples, uint32_t num_samples,
                       uint32_t sample_rate);

void wav_free(wav_t *wav);

#endif
