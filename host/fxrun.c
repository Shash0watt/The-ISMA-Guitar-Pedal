/*
 * fxrun: runs one effect over a WAV file, the same way the pedal will.
 *
 *   fxrun <effect> <in.wav> <out.wav> [knob=value ...]
 *   fxrun --list
 *
 * Audio is fed to the effect in blocks of BLOCK_SIZE samples, processed in
 * place (input == output), like the firmware's audio interrupt. Knob values
 * are 0..1, like a pot.
 *
 * Knobs can also change during the file ("automation"), to test how an effect
 * handles a knob being turned:
 *
 *   gain=0,1@2,0@4      start at 0, jump to 1 at 2 s, back to 0 at 4 s
 *
 * Changes are applied between blocks, the same way the pedal's control code
 * will update knobs between audio interrupts.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx_config.h"
#include "fx_registry.h"
#include "wav.h"

#define BLOCK_SIZE FX_BLOCK_SIZE /* same block size as the firmware */
#define PREROLL_SECONDS 0.5f
#define MAX_KNOB_EVENTS 64

/* One knob change: set knob_name to knob_value when playback reaches at_sample. */
typedef struct {
    const char *knob_name;
    float knob_value;
    float at_seconds;
    uint32_t at_sample;
} knob_event_t;

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
           "knob values are 0..1, like a pot. examples:\n"
           "  fxrun boost in.wav out.wav gain=0.7\n"
           "  fxrun boost in.wav out.wav gain=0,1@2,0@4   (knob changes at 2 s and 4 s)\n\n");
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

/* Parses one "name=value[@seconds],value[@seconds],..." argument into events.
 * Modifies the argument string in place. Returns 0 on success. */
static int parse_knob_argument(char *argument, uint32_t sample_rate, knob_event_t *events,
                               size_t *num_events)
{
    char *equals_sign = strchr(argument, '=');
    if (!equals_sign) {
        fprintf(stderr, "error: expected knob=value, got '%s'\n", argument);
        return -1;
    }
    *equals_sign = '\0'; /* split "gain=0.7" into "gain" and "0.7" */
    const char *knob_name = argument;
    char *schedule_text = equals_sign + 1;

    for (char *step_text = strtok(schedule_text, ","); step_text; step_text = strtok(NULL, ",")) {
        char *parse_end;
        float knob_value = strtof(step_text, &parse_end);
        if (parse_end == step_text) {
            fprintf(stderr, "error: '%s' is not a number (knob %s)\n", step_text, knob_name);
            return -1;
        }
        float at_seconds = 0.0f;
        if (*parse_end == '@') {
            char *time_text = parse_end + 1;
            at_seconds = strtof(time_text, &parse_end);
            if (parse_end == time_text || at_seconds < 0.0f) {
                fprintf(stderr, "error: bad time '%s' (knob %s)\n", time_text, knob_name);
                return -1;
            }
        }
        if (*parse_end != '\0') {
            fprintf(stderr, "error: can't read '%s' (knob %s); use value or value@seconds\n",
                    step_text, knob_name);
            return -1;
        }
        if (knob_value < 0.0f || knob_value > 1.0f) {
            fprintf(stderr, "warning: knobs go 0..1, %s=%g is outside that\n", knob_name,
                    (double)knob_value);
        }
        if (*num_events >= MAX_KNOB_EVENTS) {
            fprintf(stderr, "error: too many knob changes (max %d)\n", MAX_KNOB_EVENTS);
            return -1;
        }
        events[*num_events] = (knob_event_t){
            .knob_name = knob_name,
            .knob_value = knob_value,
            .at_seconds = at_seconds,
            .at_sample = (uint32_t)(at_seconds * (float)sample_rate),
        };
        (*num_events)++;
    }
    return 0;
}

/* Sorts events by time, keeping the command-line order for equal times. */
static void sort_events_by_time(knob_event_t *events, size_t num_events)
{
    for (size_t unsorted_index = 1; unsorted_index < num_events; unsorted_index++) {
        knob_event_t moving = events[unsorted_index];
        size_t insert_index = unsorted_index;
        while (insert_index > 0 && events[insert_index - 1].at_sample > moving.at_sample) {
            events[insert_index] = events[insert_index - 1];
            insert_index--;
        }
        events[insert_index] = moving;
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

    /* Everything after the file names is knob=value or a knob schedule. */
    knob_event_t knob_events[MAX_KNOB_EVENTS];
    size_t num_knob_events = 0;
    for (int arg_index = 4; arg_index < argc; arg_index++) {
        if (parse_knob_argument(argv[arg_index], sample_rate, knob_events, &num_knob_events) != 0) {
            return 2;
        }
    }
    sort_events_by_time(knob_events, num_knob_events);

    size_t state_size = effect->state_size > 0 ? effect->state_size : 1;
    void *effect_state = calloc(1, state_size);
    void *scratch_state = calloc(1, state_size);
    float *output_samples = malloc(num_samples > 0 ? num_samples * sizeof(float) : 1);
    if (!effect_state || !scratch_state || !output_samples) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    /* Check every knob name up front, on a throwaway copy of the effect, so a
     * typo is reported before any audio is processed. */
    effect->init(scratch_state, (float)sample_rate);
    for (size_t event_index = 0; event_index < num_knob_events; event_index++) {
        const knob_event_t *event = &knob_events[event_index];
        if (!effect->set_knob ||
            effect->set_knob(scratch_state, event->knob_name, event->knob_value) != 0) {
            fprintf(stderr, "error: %s has no knob '%s' (knobs: %s)\n", effect->name,
                    event->knob_name, effect->knobs);
            return 2;
        }
        if (event->at_sample >= num_samples && event->at_sample > 0) {
            fprintf(stderr, "warning: %s change at %.3f s is after the end of the file\n",
                    event->knob_name, (double)event->at_seconds);
        }
    }
    free(scratch_state);

    effect->init(effect_state, (float)sample_rate);

    /* Knob values for time 0 are set before the pre-roll. */
    size_t next_event = 0;
    while (next_event < num_knob_events && knob_events[next_event].at_sample == 0) {
        effect->set_knob(effect_state, knob_events[next_event].knob_name,
                         knob_events[next_event].knob_value);
        next_event++;
    }

    float block[BLOCK_SIZE];

    /* Run silence through first so smoothed knobs reach their starting values
     * before the real audio begins. */
    uint32_t preroll_samples = (uint32_t)(PREROLL_SECONDS * (float)sample_rate);
    for (uint32_t samples_done = 0; samples_done < preroll_samples; samples_done += BLOCK_SIZE) {
        memset(block, 0, sizeof block);
        effect->process(effect_state, block, block, BLOCK_SIZE);
    }

    /* The main loop: same job as the pedal's audio interrupt. Knob changes
     * land between blocks, like the pedal's control code. */
    for (uint32_t block_start = 0; block_start < num_samples; block_start += BLOCK_SIZE) {
        while (next_event < num_knob_events && knob_events[next_event].at_sample <= block_start) {
            effect->set_knob(effect_state, knob_events[next_event].knob_name,
                             knob_events[next_event].knob_value);
            next_event++;
        }
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
    for (size_t event_index = 0; event_index < num_knob_events; event_index++) {
        if (knob_events[event_index].at_sample > 0) {
            printf("knob change: %s -> %.2f at %.3f s\n", knob_events[event_index].knob_name,
                   (double)knob_events[event_index].knob_value,
                   (double)knob_events[event_index].at_seconds);
        }
    }
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
