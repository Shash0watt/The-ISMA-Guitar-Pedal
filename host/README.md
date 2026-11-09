# Mac test harness

Runs the effects in `api/effects/` over WAV files so they can be built and
heard without the XMC4700. Audio goes through the effect in 32-sample blocks,
processed in place, the same way the pedal's audio interrupt will do it.

## Build

```bash
make -C host
```

Tools land in `host/build/`. The `host/` folder is listed in `.cyignore`, so
ModusToolbox never compiles it into the firmware.

## Use

```bash
# make test audio (until you have a dry guitar recording)
host/build/gen_test pluck host/out/pluck.wav
host/build/gen_test sine 220 host/out/sine.wav
host/build/gen_test sweep host/out/sweep.wav

# run an effect: knob values are 0..1, like a pot
host/build/fxrun boost host/out/pluck.wav host/out/boost.wav gain=0.7

# list effects and their knobs
host/build/fxrun --list
```

`host/out/` is git-ignored, so test output doesn't get committed.

fxrun reads any common WAV (16/24/32-bit, float, mono or stereo) and writes
mono 32-bit float WAV. It prints the input/output peak levels and warns when the
output goes above 1.0 (would clip on the DAC) or contains NaN/infinite values
(usually a bug in the effect).

## Adding an effect

1. Write `fx_<name>.h` / `fx_<name>.c` in `api/effects/` (picked up automatically).
2. In `host/fx_registry.c`, include the header, write the three small wrappers
   and add a line to `fx_registry[]`. A commented boost example is there to copy.
3. `make -C host`, then run it with `fxrun`.

## Notes

- **Sanitizers are on.** If an effect reads or writes past the end of a buffer,
  fxrun stops with an AddressSanitizer report pointing at the line.
- **Pre-roll.** Before the real audio, fxrun runs 0.5 s of silence through the
  effect so smoothed knob values settle on the values you passed.
- **Sample rate.** The effect's `init` gets the file's actual sample rate.
  `gen_test` always writes 48 kHz.
