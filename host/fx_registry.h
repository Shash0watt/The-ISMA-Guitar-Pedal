/* The list of effects that fxrun can run. Add yours in fx_registry.c. */
#ifndef FX_REGISTRY_H
#define FX_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;   /* what you type on the command line, e.g. "boost" */
    const char *knobs;  /* help text listing the knob names, e.g. "gain" */
    size_t state_size;  /* sizeof(your effect's struct) */
    void (*init)(void *state, float sample_rate);
    /* Return 0 if the knob exists, -1 if not. Can be NULL if there are no knobs. */
    int (*set_knob)(void *state, const char *knob_name, float knob_value);
    void (*process)(void *state, const float *input, float *output, uint32_t num_samples);
} fx_entry_t;

extern const fx_entry_t fx_registry[];
extern const size_t fx_registry_count;

const fx_entry_t *fx_find(const char *effect_name);

#endif
