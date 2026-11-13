# System overview

_Last updated: 2026-11-12. If the code has changed since then, check this page
against it before trusting the details._

The whole pedal on one page: what is wired to the board, and what the firmware
does with it. For a closer look at the audio path and the knobs, see
[Dataflow](dataflow.md).

```mermaid
flowchart TB
    subgraph inputs["Going in (outside the board)"]
        source["Guitar or line source"] --> input_circuit["Input circuit<br/>10k/10k divider, bias to 1.65 V"]
        encoders["4 KY-040 encoders<br/>turn and press"]
    end

    subgraph board["XMC4700 Relax Kit"]
        subgraph pins_in["Input pins and peripherals"]
            adc["ADC<br/>P14.0, 12-bit"]
            encoder_pins["Encoder pins<br/>P0.0 to P0.11"]
            onboard_buttons["BUTTON1 P15.13<br/>BUTTON2 P15.12"]
        end

        subgraph interrupt["Audio interrupt (api/audio): 96,000 times a second"]
            read_sample["Read one sample<br/>api/adc"] --> run_effect["Run the current effect,<br/>or play a test tone or silence<br/>api/pedal, api/effects"] --> write_sample["Write one sample<br/>api/dac"]
        end

        subgraph main_loop["Main loop: every 1 ms"]
            poll_inputs["Read the knobs<br/>poll_inputs()"] --> poll_pedal["Send knob changes<br/>poll_pedal()"]
            status["Every 50 ms: buttons,<br/>meters and LEDs"]
        end

        settings["Shared settings<br/>current effect, knob targets, test mode"]

        subgraph pins_out["Output pins and peripherals"]
            dac["DAC<br/>P14.8, 12-bit"]
            onboard_leds["LED1 P5.9<br/>LED2 P5.8"]
            uart["Debug UART<br/>P1.5 TX, 115200 baud"]
        end
    end

    subgraph outputs["Coming out (outside the board)"]
        output_circuit["Output circuit"] --> listener["Amp or<br/>headphones"]
        laptop["Laptop over USB<br/>shows printf text"]
    end

    input_circuit --> adc --> read_sample
    write_sample --> dac --> output_circuit

    encoders --> encoder_pins --> poll_inputs
    onboard_buttons --> status --> onboard_leds

    poll_pedal -.-> settings
    status -.-> settings
    settings -. "read on every sample" .-> run_effect
    write_sample -. "sample count: the 1 ms clock" .-> poll_inputs
    write_sample -. "meters" .-> status

    poll_inputs -- "printf" --> uart
    status -- "printf" --> uart
    uart --> laptop

    classDef hardware fill:#F1EFE8,stroke:#5F5E5A,color:#444441
    classDef firmware fill:#EEEDFE,stroke:#534AB7,color:#3C3489
    classDef effects fill:#FAECE7,stroke:#993C1D,color:#712B13
    class source,input_circuit,encoders,output_circuit,listener,laptop hardware
    class adc,encoder_pins,onboard_buttons,dac,onboard_leds,uart hardware
    class read_sample,write_sample,poll_inputs,poll_pedal,status,settings firmware
    class run_effect effects
```

Colors: gray = hardware, purple = firmware, coral = effects code.
Solid arrows are signals and function calls. Dotted arrows are values the two
parts of the firmware share in memory (listed in the table below).

## How it works

The firmware has two parts running at very different speeds.

**The audio interrupt** is the fast one. A timer fires 96,000 times a second,
and each time the chip reads one sample from the ADC, runs the current effect
on it, and writes the result to the DAC. There are about 1,500 processor cycles
per sample in total, and an effect should use no more than about 1,200 of them,
so it only does quick math and never waits for anything.

**The main loop** is the slow one. It has no timer of its own: it counts audio
samples, and every 96 samples (1 ms) it reads the encoders and passes any knob
change on to the current effect. Every 50 ms it also checks the two on-board
buttons and updates the two LEDs. `printf` is only ever called from here.

At power-on the pedal plays a 1 kHz beep for one second, then switches to
normal playing. If you hear the beep, the DAC and the output circuit work.

The two parts never call each other's functions. They only share a few small
values:

| Shared value | Written by | Read by |
|---|---|---|
| Which effect is on | Main loop (BUTTON2) | Audio interrupt |
| Knob targets (gain, drive, level) | Main loop (encoders, and BUTTON2 restoring an effect's saved knobs) | Audio interrupt, which glides toward them |
| Test mode (normal, tone, silence) | Main loop (BUTTON1, and the boot beep) | Audio interrupt |
| Sample count | Audio interrupt | Main loop, as its 1 ms clock |
| Input average and output peak | Audio interrupt | Main loop, every 50 ms, for LED2 |

## What each part does

| Part | Job |
|---|---|
| Input circuit | Lifts the audio signal to sit around 1.65 V, the middle of the ADC's range |
| ADC | Turns the input voltage into a number, 0 to 4095 |
| Effect | Changes the sample: boost makes it louder, overdrive distorts it, bypass leaves it alone |
| DAC | Turns the number back into a voltage |
| Encoders | Four knobs, each 0.0 to 1.0 in 40 clicks. `api/pedal` decides what they control: boost uses knob 0 for gain; overdrive uses knob 0 for drive and knob 1 for level. Each effect remembers where its knobs were left |
| BUTTON1 | Cycles the test mode: normal, 1 kHz test tone, silence. Effects are only heard in normal mode |
| BUTTON2 | Steps to the next effect: bypass, boost, overdrive |
| LED1 | Shows the test mode: slow blink = normal, fast blink = tone, solid = silence |
| LED2 | Blinks fast if the input bias is wrong; otherwise lights when there is output signal |
| Debug UART | Carries `printf` text to the laptop through the board's USB debugger |

Four more button inputs (P0.12 to P0.15) are read by the firmware but nothing
uses them yet.
