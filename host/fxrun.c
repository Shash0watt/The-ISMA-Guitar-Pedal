/*
 * fxrun: runs one effect over a WAV file, the same way the pedal will.
 *
 *   fxrun <effect> <in.wav> <out.wav> [knob=value ...]
 *   fxrun --list
 *
 * Audio is fed to the effect in blocks of BLOCK_SIZE samples, processed in
 * place (input == output), like the firmware's audio interrupt. Knob values
 * are 0..1, like a pot.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx_registry.h"
#include "wav.h"

#define BLOCK_SIZE 32
#define PREROLL_SECONDS 0.5f

static void list_effects(void)
{
    printf("effects:\n");
    for (size_t entry_index = 0; entry_index < fx_registry_count; entry_index++) {
        printf("  %-14s knobs: %s\n", fx_registry[entry_index].name,
               fx_registry[entry_index].knobs);
    }
}

static void print_usage(void)
{
    printf("usage: fxrun <effect> <in.wav> <out.wav> [knob=value ...]\n"
           "       fxrun --list\n\n"
           "knob values are 0..1, like a pot. example:\n"
           "  fxrun boost in.wav out.wav gain=0.7\n\n");
    list_effects();
}

static void print_level(const char *label, float peak)
{
    if (peak > 0.0f) {
        printf("%s %6.1f dBFS  (peak %.3f)\n", label, (double)(20.0f * log10f(peak)),
               (double)peak);
    } else {
        printf("%s silent\n", label);
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "--list") == 0 || strcmp(argv[1], "-l") == 0)) {
        list_effects();
        return 0;
    }
    if (argc < 4) {
        print_usage();
        return 2;
    }
    const char *effect_name = argv[1];
    const char *input_path = argv[2];
    const char *output_path = argv[3];

    const fx_entry_t *effect = fx_find(effect_name);
    if (!effect) {
        fprintf(stderr, "error: unknown effect '%s'\n\n", effect_name);
        list_effects();
        return 2;
    }

    wav_t input_wav;
    if (wav_read_mono(input_path, &input_wav) != 0) return 1;
    uint32_t num_samples = input_wav.num_samples;
    uint32_t sample_rate = input_wav.sample_rate;

    void *effect_state = calloc(1, effect->state_size > 0 ? effect->state_size : 1);
    float *output_samples = malloc(num_samples > 0 ? num_samples * sizeof(float) : 1);
    if (!effect_state || !output_samples) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    effect->init(effect_state, (float)sample_rate);

    /* Everything after the file names is knob=value. */
    for (int arg_index = 4; arg_index < argc; arg_index++) {
        char *equals_sign = strchr(argv[arg_index], '=');
        if (!equals_sign) {
            fprintf(stderr, "error: expected knob=value, got '%s'\n", argv[arg_index]);
            return 2;
        }
        *equals_sign = '\0'; /* split "gain=0.7" into "gain" and "0.7" */
        const char *knob_name = argv[arg_index];
        const char *value_text = equals_sign + 1;
        char *parse_end;
        float knob_value = strtof(value_text, &parse_end);
        if (parse_end == value_text || *parse_end != '\0') {
            fprintf(stderr, "error: '%s' is not a number (knob %s)\n", value_text, knob_name);
            return 2;
        }
        if (knob_value < 0.0f || knob_value > 1.0f) {
            fprintf(stderr, "warning: knobs go 0..1, %s=%g is outside that\n", knob_name,
                    (double)knob_value);
        }
        if (!effect->set_knob || effect->set_knob(effect_state, knob_name, knob_value) != 0) {
            fprintf(stderr, "error: %s has no knob '%s' (knobs: %s)\n", effect->name,
                    knob_name, effect->knobs);
            return 2;
        }
    }

    float block[BLOCK_SIZE];

    /* Run silence through first so smoothed knobs reach the values set above
     * before the real audio starts. Otherwise the start of out.wav would glide
     * from the default knob values. */
    uint32_t preroll_samples = (uint32_t)(PREROLL_SECONDS * (float)sample_rate);
    for (uint32_t samples_done = 0; samples_done < preroll_samples; samples_done += BLOCK_SIZE) {
        memset(block, 0, sizeof block);
        effect->process(effect_state, block, block, BLOCK_SIZE);
    }

    /* The main loop: same job as the pedal's audio interrupt. */
    for (uint32_t block_start = 0; block_start < num_samples; block_start += BLOCK_SIZE) {
        uint32_t samples_left = num_samples - block_start;
        uint32_t block_length = samples_left < BLOCK_SIZE ? samples_left : BLOCK_SIZE;
        memcpy(block, input_wav.samples + block_start, block_length * sizeof(float));
        effect->process(effect_state, block, block, block_length);
        memcpy(output_samples + block_start, block, block_length * sizeof(float));
    }

    /* Check the output for clipping and broken values. */
    float peak_input = 0.0f, peak_output = 0.0f;
    uint32_t samples_over_full_scale = 0, broken_samples = 0, first_broken_index = 0;
    for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++) {
        peak_input = fmaxf(peak_input, fabsf(input_wav.samples[sample_index]));
        if (!isfinite(output_samples[sample_index])) {
            if (broken_samples == 0) first_broken_index = sample_index;
            broken_samples++;
            output_samples[sample_index] = 0.0f;
            continue;
        }
        float magnitude = fabsf(output_samples[sample_index]);
        peak_output = fmaxf(peak_output, magnitude);
        if (magnitude > 1.0f) samples_over_full_scale++;
    }

    printf("%s: %u samples at %u Hz (%.2f s), %s input, blocks of %d\n", effect->name,
           num_samples, sample_rate, (double)num_samples / (double)sample_rate,
           input_wav.num_channels == 1 ? "mono" : "stereo->mono", BLOCK_SIZE);
    print_level("in: ", peak_input);
    print_level("out:", peak_output);
    if (samples_over_full_scale > 0) {
        printf("warning: %u samples are above 1.0 and would clip on the DAC\n",
               samples_over_full_scale);
    }
    if (broken_samples > 0) {
        printf("ERROR: %u samples were NaN or infinite (first at %.3f s); written as 0\n",
               broken_samples, (double)first_broken_index / (double)sample_rate);
    }

    int exit_code = 0;
    if (wav_write_mono_f32(output_path, output_samples, num_samples, sample_rate) == 0) {
        printf("wrote %s\n", output_path);
    } else {
        exit_code = 1;
    }
    if (broken_samples > 0) exit_code = 3;

    wav_free(&input_wav);
    free(output_samples);
    free(effect_state);
    return exit_code;
}
