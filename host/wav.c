/*
 * Minimal WAV reader/writer. Assumes a little-endian machine (every Mac is).
 *
 * A WAV file is a 12-byte header ("RIFF", size, "WAVE") followed by chunks.
 * Each chunk is a 4-letter tag, a 4-byte size, then that many bytes. We only
 * need two chunks: "fmt " (sample format) and "data" (the samples).
 */
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAV_FORMAT_PCM 1
#define WAV_FORMAT_FLOAT 3
#define WAV_FORMAT_EXTENSIBLE 0xFFFE

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/* Converts one stored sample to a float in -1..1. */
static float sample_to_float(const uint8_t *bytes, uint16_t audio_format,
                             uint16_t bits_per_sample)
{
    if (audio_format == WAV_FORMAT_FLOAT) {
        if (bits_per_sample == 32) {
            float float_value;
            memcpy(&float_value, bytes, 4);
            return float_value;
        }
        double double_value;
        memcpy(&double_value, bytes, 8);
        return (float)double_value;
    }
    switch (bits_per_sample) {
    case 8: /* 8-bit WAV is unsigned, centred on 128 */
        return ((float)bytes[0] - 128.0f) / 128.0f;
    case 16:
        return (float)(int16_t)read_u16(bytes) / 32768.0f;
    case 24: {
        /* Put the 3 bytes in the top of a 32-bit int, then shift back down so
         * the sign bit is kept. */
        int32_t value = (int32_t)(((uint32_t)bytes[0] << 8) | ((uint32_t)bytes[1] << 16) |
                                  ((uint32_t)bytes[2] << 24)) >> 8;
        return (float)value / 8388608.0f;
    }
    default: /* 32-bit */
        return (float)(int32_t)read_u32(bytes) / 2147483648.0f;
    }
}

int wav_read_mono(const char *path, wav_t *wav)
{
    memset(wav, 0, sizeof *wav);

    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "error: can't open %s\n", path);
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *file_bytes = file_size > 0 ? malloc((size_t)file_size) : NULL;
    if (!file_bytes || fread(file_bytes, 1, (size_t)file_size, file) != (size_t)file_size) {
        fprintf(stderr, "error: can't read %s\n", path);
        fclose(file);
        free(file_bytes);
        return -1;
    }
    fclose(file);

    int result = -1;
    if (file_size < 12 || memcmp(file_bytes, "RIFF", 4) != 0 ||
        memcmp(file_bytes + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "error: %s is not a WAV file\n", path);
        goto done;
    }

    uint16_t audio_format = 0, num_channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
    const uint8_t *sample_data = NULL;
    uint32_t sample_data_size = 0;

    /* Walk the chunks; only "fmt " and "data" matter, the rest are skipped. */
    size_t offset = 12;
    while (offset + 8 <= (size_t)file_size) {
        const uint8_t *chunk = file_bytes + offset;
        const uint8_t *chunk_body = chunk + 8;
        uint32_t chunk_size = read_u32(chunk + 4);
        size_t bytes_left = (size_t)file_size - (offset + 8);
        if (chunk_size > bytes_left) chunk_size = (uint32_t)bytes_left; /* truncated file */

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            audio_format = read_u16(chunk_body);
            num_channels = read_u16(chunk_body + 2);
            sample_rate = read_u32(chunk_body + 4);
            bits_per_sample = read_u16(chunk_body + 14);
            /* The "extensible" format keeps the real format code further in. */
            if (audio_format == WAV_FORMAT_EXTENSIBLE && chunk_size >= 26) {
                audio_format = read_u16(chunk_body + 24);
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            sample_data = chunk_body;
            sample_data_size = chunk_size;
        }
        offset += 8 + (size_t)chunk_size + (chunk_size & 1u); /* chunks are 2-byte aligned */
    }

    if (!sample_data || num_channels == 0 || sample_rate == 0) {
        fprintf(stderr, "error: %s is missing its fmt or data section\n", path);
        goto done;
    }
    int supported = (audio_format == WAV_FORMAT_PCM &&
                     (bits_per_sample == 8 || bits_per_sample == 16 ||
                      bits_per_sample == 24 || bits_per_sample == 32)) ||
                    (audio_format == WAV_FORMAT_FLOAT &&
                     (bits_per_sample == 32 || bits_per_sample == 64));
    if (!supported) {
        fprintf(stderr, "error: %s uses an unsupported format (code %u, %u-bit)\n", path,
                (unsigned)audio_format, (unsigned)bits_per_sample);
        goto done;
    }

    uint32_t bytes_per_sample = bits_per_sample / 8u;
    uint32_t num_samples = sample_data_size / (num_channels * bytes_per_sample);
    float *samples = malloc(num_samples > 0 ? num_samples * sizeof(float) : 1);
    if (!samples) {
        fprintf(stderr, "error: out of memory reading %s\n", path);
        goto done;
    }
    /* Average all channels into one, since guitar is mono. */
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        float channel_sum = 0.0f;
        for (uint16_t channel_index = 0; channel_index < num_channels; channel_index++) {
            size_t byte_offset =
                ((size_t)sample_index * num_channels + channel_index) * bytes_per_sample;
            channel_sum +=
                sample_to_float(sample_data + byte_offset, audio_format, bits_per_sample);
        }
        samples[sample_index] = channel_sum / (float)num_channels;
    }

    wav->samples = samples;
    wav->num_samples = num_samples;
    wav->sample_rate = sample_rate;
    wav->num_channels = num_channels;
    result = 0;

done:
    free(file_bytes);
    return result;
}

static void write_u16(FILE *file, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    fwrite(bytes, 1, 2, file);
}

static void write_u32(FILE *file, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    fwrite(bytes, 1, 4, file);
}

int wav_write_mono_f32(const char *path, const float *samples, uint32_t num_samples,
                       uint32_t sample_rate)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "error: can't create %s\n", path);
        return -1;
    }
    uint32_t sample_data_size = num_samples * 4u;

    fwrite("RIFF", 1, 4, file);
    write_u32(file, 4u + (8u + 18u) + (8u + 4u) + (8u + sample_data_size));
    fwrite("WAVE", 1, 4, file);

    fwrite("fmt ", 1, 4, file);
    write_u32(file, 18);
    write_u16(file, WAV_FORMAT_FLOAT);
    write_u16(file, 1);                /* mono */
    write_u32(file, sample_rate);
    write_u32(file, sample_rate * 4u); /* bytes per second */
    write_u16(file, 4);                /* bytes per sample frame */
    write_u16(file, 32);               /* bits per sample */
    write_u16(file, 0);                /* no extra format bytes */

    fwrite("fact", 1, 4, file);        /* required for non-PCM WAV */
    write_u32(file, 4);
    write_u32(file, num_samples);

    fwrite("data", 1, 4, file);
    write_u32(file, sample_data_size);
    fwrite(samples, sizeof(float), num_samples, file);

    int write_failed = ferror(file);
    if (fclose(file) != 0 || write_failed) {
        fprintf(stderr, "error: failed writing %s\n", path);
        return -1;
    }
    return 0;
}

void wav_free(wav_t *wav)
{
    free(wav->samples);
    wav->samples = NULL;
    wav->num_samples = 0;
}
