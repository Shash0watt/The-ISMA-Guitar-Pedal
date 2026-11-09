# Dataflow

How audio moves through the pedal, and how the same effects code is tested on a
computer before it runs on the XMC4700.

## Overview: one effects library, two homes

The code in `effects/` is plain C with no hardware access. On the Mac it is fed
from WAV files; on the pedal it is fed from the ADC. Only the outer boxes change.

```mermaid
flowchart TB
    subgraph mac["Mac test harness (host/)"]
        direction TB
        inwav["in.wav<br/>dry guitar recording"] --> rd["dr_wav read<br/>float samples -1..1"]
        wr["dr_wav write<br/>float WAV"] --> outwav["out.wav<br/>listen and compare"]
    end

    subgraph fx["effects/ (same C code on both)"]
        direction LR
        boost["Boost"] --> od["Overdrive"] --> trem["Tremolo"] --> dly["Delay"]
    end

    subgraph pedal["Pedal firmware (XMC4700)"]
        direction TB
        guitar["Guitar<br/>through input buffer"] --> adc["ADC + DMA<br/>fills 32-sample blocks"]
        dac["DAC + DMA<br/>sends block out"] --> amp["Amp<br/>through output filter"]
    end

    rd --> boost
    adc --> boost
    dly --> wr
    dly --> dac

    classDef host fill:#E1F5EE,stroke:#0F6E56,color:#085041
    classDef fw fill:#EEEDFE,stroke:#534AB7,color:#3C3489
    classDef shared fill:#FAECE7,stroke:#993C1D,color:#712B13
    class inwav,rd,wr,outwav host
    class guitar,adc,dac,amp fw
    class boost,od,trem,dly shared
```

## Pedal: real-time signal path

Audio moves in blocks. DMA fills one half of a buffer while the CPU processes the
other half, then they swap. Knobs are read separately at a much slower rate.

```mermaid
flowchart TB
    subgraph audio_in["Audio in"]
        direction LR
        g["Guitar<br/>pickup signal"] --> ibuf["Input buffer<br/>bias to mid-rail"] --> adc["ADC / codec<br/>48 kHz"] --> dmain["DMA in buffer<br/>ping-pong halves"]
    end

    subgraph isr["Audio interrupt: every 32 samples (0.67 ms)"]
        direction LR
        i2f["Int to float<br/>scale to -1..1"] --> chain["Effects chain<br/>effects/ code"] --> f2i["Float to int<br/>clamp, scale to DAC"]
    end

    subgraph audio_out["Audio out"]
        direction LR
        dmaout["DMA out buffer<br/>ping-pong halves"] --> dac["DAC / codec<br/>48 kHz"] --> ofilt["Output filter<br/>smooth, block DC"] --> amp["Amp<br/>or headphones"]
    end

    subgraph controls["Controls (slow, about 100 Hz)"]
        direction LR
        pots["Pots + footswitch<br/>slow ADC, GPIO"] --> mainloop["Main loop<br/>set_knob(), bypass"]
    end

    dmain --> i2f
    f2i --> dmaout
    mainloop -. "knob values" .-> chain

    classDef analog fill:#F1EFE8,stroke:#5F5E5A,color:#444441
    classDef fw fill:#EEEDFE,stroke:#534AB7,color:#3C3489
    classDef shared fill:#FAECE7,stroke:#993C1D,color:#712B13
    class g,ibuf,ofilt,amp,pots analog
    class adc,dmain,i2f,f2i,dmaout,dac,mainloop fw
    class chain shared
```

Colors: gray = analog hardware, purple = XMC4700 firmware, coral = shared effects
code, teal = Mac-only test code.

## Key numbers

| | Value | Why |
|---|---|---|
| Sample rate | 48 kHz | Covers hearing range (up to 24 kHz) with room for the anti-alias filter; standard for codecs |
| Block size | 32 samples per half | 0.67 ms per block keeps latency low; Lab7 used 256 (about 5 ms) |
| CPU budget | about 96,000 cycles per block | 144 MHz / 48 kHz = 3,000 cycles per sample |
| Sample format | `float`, -1..1 | Cortex-M4F has a single-precision FPU; `double` runs in software |
| Knob rate | about 100 Hz | Knobs are slow; values are smoothed inside each effect to avoid clicks |
