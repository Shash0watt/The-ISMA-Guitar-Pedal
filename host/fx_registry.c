/*
 * The effects fxrun knows about.
 *
 * To add an effect:
 *   1. #include its header
 *   2. write three small wrappers (copy the boost example below)
 *   3. add a line to fx_registry[]
 *
 * The wrappers exist because fxrun handles every effect the same way (void *
 * state, knobs by name), while each effect has its own struct and setters.
 */
#include "fx_registry.h"
#include "../api/effects/fx_boost.h"
#include "fx_overdrive.h"

#include <string.h>

/* passthrough: output = input. Checks the harness itself: out.wav should be
 * identical to in.wav. */
static void passthrough_init(void *state, float sample_rate)
{
    (void)state;
    (void)sample_rate;
}

static void passthrough_process(void *state, const float *input, float *output,
                                uint32_t num_samples)
{
    (void)state;
    if (input != output) memmove(output, input, num_samples * sizeof *output);
}


static void boost_init(void *state, float sample_rate)
{
    fx_boost_init(state, sample_rate);
}

static void boost_process(void *state, const float *input, float *output,
                                uint32_t num_samples)
{
    fx_boost_process(state, input, output, num_samples);
}


static int boost_set(void *state, const char *knob_name, float knob_value)
{
    if (strcmp(knob_name, "gain") == 0) {
        fx_boost_set_gain(state, knob_value);
        return 0;
    }
    return -1;
}

static void overdrive_init(void *state, float sample_rate)
{
    fx_overdrive_init(state, sample_rate);
}

static int overdrive_set(void *state, const char *knob_name, float knob_value)
{
    if (strcmp(knob_name, "drive") == 0) {
        fx_overdrive_set_drive(state, knob_value);
        return 0;
    }
    if (strcmp(knob_name, "level") == 0) {
        fx_overdrive_set_level(state, knob_value);
        return 0;
    }
    return -1;
}

static void overdrive_process(void *state, const float *input, float *output,
                              uint32_t num_samples)
{
    fx_overdrive_process(state, input, output, num_samples);
}

const fx_entry_t fx_registry[] = {
    {"passthrough", "(none)", 1, passthrough_init, NULL, passthrough_process},
    {"boost", "gain", sizeof(fx_boost_t), boost_init, boost_set, boost_process},
    {"overdrive", "drive level", sizeof(fx_overdrive_t), overdrive_init, overdrive_set,
     overdrive_process},
};

const size_t fx_registry_count = sizeof fx_registry / sizeof fx_registry[0];

const fx_entry_t *fx_find(const char *effect_name)
{
    for (size_t entry_index = 0; entry_index < fx_registry_count; entry_index++) {
        if (strcmp(fx_registry[entry_index].name, effect_name) == 0) {
            return &fx_registry[entry_index];
        }
    }
    return NULL;
}
