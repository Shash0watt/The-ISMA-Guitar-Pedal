/*
 * gen_test: makes test WAV files (48 kHz, mono, float).
 *
 *   gen_test pluck out.wav        guitar-like plucked notes and a strummed chord
 *   gen_test sine <hz> out.wav    2 s sine wave at -6 dBFS
 *   gen_test sweep out.wav        5 s sweep from 20 Hz to 20 kHz
 *
 * pluck is a stand-in until you have a real dry guitar recording. sine makes
 * clipping easy to see. sweep shows aliasing in Audacity's spectrogram view.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wav.h"

#define SAMPLE_RATE 48000u
#define PI 3.14159265358979323846

static uint32_t random_state = 12345u;

/* Pseudo-random number in -1..1 (same sequence every run). */
static float random_noise(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return (float)(random_state >> 8) / 8388608.0f - 1.0f;
}

/* Karplus-Strong plucked string: a noise burst circulating in a short delay
 * line, low-passed on each pass so it decays like a string. The line length
 * sets the pitch. */
static void add_pluck(float *samples, uint32_t num_samples, double start_seconds,
                      double duration_seconds, double frequency_hz, float amplitude)
{
    uint32_t start_sample = (uint32_t)(start_seconds * SAMPLE_RATE);
    uint32_t duration_samples = (uint32_t)(duration_seconds * SAMPLE_RATE);
    uint32_t line_length = (uint32_t)((double)SAMPLE_RATE / frequency_hz + 0.5);
    float *delay_line = malloc(line_length * sizeof *delay_line);
    if (!delay_line) return;

    for (uint32_t line_index = 0; line_index < line_length; line_index++) {
        delay_line[line_index] = random_noise();
    }
    for (uint32_t line_index = 1; line_index < line_length; line_index++) {
        /* soften the noise burst so it sounds less like a pick scrape */
        delay_line[line_index] = 0.5f * (delay_line[line_index] + delay_line[line_index - 1]);
    }

    uint32_t release_samples = SAMPLE_RATE / 100; /* 10 ms fade so notes don't click off */
    uint32_t read_index = 0;
    for (uint32_t offset = 0; offset < duration_samples && start_sample + offset < num_samples;
         offset++) {
        uint32_t next_index = (read_index + 1) % line_length;
        float string_value = delay_line[read_index];
        delay_line[read_index] =
            0.998f * 0.5f * (delay_line[read_index] + delay_line[next_index]);
        float envelope = offset + release_samples > duration_samples
                             ? (float)(duration_samples - offset) / (float)release_samples
                             : 1.0f;
        samples[start_sample + offset] += amplitude * envelope * string_value;
        read_index = next_index;
    }
    free(delay_line);
}

static void normalize(float *samples, uint32_t num_samples, float target_peak)
{
    float peak = 0.0f;
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        peak = fmaxf(peak, fabsf(samples[sample_index]));
    }
    if (peak > 0.0f) {
        for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
            samples[sample_index] *= target_peak / peak;
        }
    }
}

/* 10 ms fade in and out so the file doesn't start or end with a click. */
static void fade_edges(float *samples, uint32_t num_samples)
{
    uint32_t fade_samples = SAMPLE_RATE / 100;
    for (uint32_t offset = 0; offset < fade_samples && offset < num_samples; offset++) {
        float fade_gain = (float)offset / (float)fade_samples;
        samples[offset] *= fade_gain;
        samples[num_samples - 1 - offset] *= fade_gain;
    }
}

static int make_pluck(const char *path)
{
    uint32_t num_samples = (uint32_t)(8.5 * SAMPLE_RATE);
    float *samples = calloc(num_samples, sizeof *samples);
    if (!samples) return 1;

    /* a low riff: {start seconds, frequency Hz} */
    const double riff[][2] = {
        {0.0, 82.41}, {0.4, 82.41}, {0.8, 98.00}, {1.2, 110.00}, {1.8, 82.41}, {2.2, 123.47},
    };
    for (size_t note_index = 0; note_index < sizeof riff / sizeof riff[0]; note_index++) {
        add_pluck(samples, num_samples, riff[note_index][0], 0.38, riff[note_index][1], 0.8f);
    }

    /* E major chord, strummed low string to high string */
    const double chord_hz[] = {82.41, 123.47, 164.81, 207.65, 246.94, 329.63};
    for (size_t string_index = 0; string_index < 6; string_index++) {
        add_pluck(samples, num_samples, 3.0 + 0.015 * (double)string_index, 2.8,
                  chord_hz[string_index], 0.5f);
    }

    /* a few single high notes: {start seconds, frequency Hz} */
    const double lead[][2] = {{6.0, 329.63}, {6.5, 392.00}, {7.0, 440.00}, {7.5, 493.88}};
    for (size_t note_index = 0; note_index < sizeof lead / sizeof lead[0]; note_index++) {
        add_pluck(samples, num_samples, lead[note_index][0], 0.9, lead[note_index][1], 0.6f);
    }

    normalize(samples, num_samples, 0.5f); /* about -6 dBFS, a typical DI level */
    int write_result = wav_write_mono_f32(path, samples, num_samples, SAMPLE_RATE);
    free(samples);
    return write_result == 0 ? 0 : 1;
}

static int make_sine(double frequency_hz, const char *path)
{
    uint32_t num_samples = 2 * SAMPLE_RATE;
    float *samples = malloc(num_samples * sizeof *samples);
    if (!samples) return 1;
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        double time_seconds = (double)sample_index / SAMPLE_RATE;
        samples[sample_index] = (float)(0.5 * sin(2.0 * PI * frequency_hz * time_seconds));
    }
    fade_edges(samples, num_samples);
    int write_result = wav_write_mono_f32(path, samples, num_samples, SAMPLE_RATE);
    free(samples);
    return write_result == 0 ? 0 : 1;
}

/* Exponential sweep: frequency rises by the same ratio every second, so each
 * octave gets equal time. */
static int make_sweep(const char *path)
{
    const double start_hz = 20.0, end_hz = 20000.0, duration_seconds = 5.0;
    uint32_t num_samples = (uint32_t)(duration_seconds * SAMPLE_RATE);
    float *samples = malloc(num_samples * sizeof *samples);
    if (!samples) return 1;
    double log_ratio = log(end_hz / start_hz);
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        double time_seconds = (double)sample_index / SAMPLE_RATE;
        double phase = 2.0 * PI * start_hz * duration_seconds / log_ratio *
                       (exp(time_seconds / duration_seconds * log_ratio) - 1.0);
        samples[sample_index] = (float)(0.5 * sin(phase));
    }
    fade_edges(samples, num_samples);
    int write_result = wav_write_mono_f32(path, samples, num_samples, SAMPLE_RATE);
    free(samples);
    return write_result == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    int exit_code = 2; /* 2 = bad arguments */
    if (argc == 3 && strcmp(argv[1], "pluck") == 0) {
        exit_code = make_pluck(argv[2]);
    } else if (argc == 4 && strcmp(argv[1], "sine") == 0) {
        double frequency_hz = atof(argv[2]);
        if (frequency_hz > 0.0 && frequency_hz < SAMPLE_RATE / 2.0) {
            exit_code = make_sine(frequency_hz, argv[3]);
        }
    } else if (argc == 3 && strcmp(argv[1], "sweep") == 0) {
        exit_code = make_sweep(argv[2]);
    }

    if (exit_code == 2) {
        fprintf(stderr, "usage: gen_test pluck out.wav\n"
                        "       gen_test sine <hz> out.wav\n"
                        "       gen_test sweep out.wav\n");
    } else if (exit_code == 0) {
        printf("wrote %s\n", argv[argc - 1]);
    }
    return exit_code;
}
