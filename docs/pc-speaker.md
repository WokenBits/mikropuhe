# The virtual PC speaker

MikroPuhe was written for the PC speaker: a one-bit output, on or off. `puhu --device pcspeaker` plays the speech the way the original's player drove that bit, instead of handing the 8-bit samples to a D/A converter.

```bash
puhu -d pcspeaker "hyvää päivää"                      # a 386, through a small loudspeaker
puhu -d pcspeaker --machine xt "hyvää päivää"         # the same on a 4.77 MHz PC/XT
puhu -d pcspeaker --machine ideal --speaker none ...  # exact pulses, no loudspeaker
puhu --list-devices
```

This page says what is taken from the original, what is a model of hardware, and how much to trust each part. The original's code is described in [original-program.md](original-program.md#output-stage-the-pc-speaker).

## What the original did

For every 8-bit sample the player (0x15616) switches the speaker on, waits, switches it off, and waits for the rest of a fixed period of 45,454 ns. The on-time carries the sample. Waiting is done by counting passes of a `LOOP` instruction; how many passes make a given time was measured at start-up (0x15167), and a table of 257 pass counts is built from the measurement (0x152b7).

Two numbers describe the machine:

| | Stored at | Meaning |
|---|---|---|
| loop time | DS:0x1dd4 | nanoseconds for one pass of `LOOP` |
| overhead | DS:0x1dd8 | nanoseconds for one sample through the player's loop with both waits at one pass |

For a wanted on-time *d* the table holds `round((d − overhead) / loop) + 1` passes, or 1 if *d* is too short to time at all. The program refuses to run when loop + overhead reach 45,454 ns ("Tämä laitteisto on liian hidas puhumiseen").

## What is ported, and how it is checked

`mp_pcspk_table` in [`port/src/pcspk.c`](../port/src/pcspk.c) is the table builder, and `mp_pcspk_samples` follows the player: on for `table[sample + 1]` passes, off for `table[0] − table[sample + 1]`.

`make -C port check-pcspk` runs the original's two routines unmodified in the emulator (`tools/pcspk_oracle.py`), for the five machine presets and seven other calibrations including the edge of "too slow". It compares the 257 table entries, and for each of the 255 sample values the number of `LOOP` passes the original's player executes between its writes to port 0x61, with the C code.

**Result: 12 calibrations, 0 differences.**

So for given calibration values, the pattern of passes is the original's. What follows is not from the original.

## The model: from passes to a signal

### Time

A pass count becomes a time with three assumptions:

1. One pass takes the loop time. (That is what the calibration measured.)
2. Of the fixed overhead, about 30% lies between "speaker on" and "speaker off" and 70% after. This comes from the player's loop: 6 of its 17 instructions lie between the two port writes, and by their timings they take a little under a third of a pass. The original's table is built as if all of the overhead lay inside the on-time, so **every pulse is shorter than intended by 70% of the overhead**. On a fast machine that is nothing; on a slow one it shifts and squeezes the whole signal.
3. The real player does a little more per sample than the loop that was calibrated (it advances a pointer and tests the abort flag, 0x15648 and 0x1564d). That time is added to every sample as `extra`, so slow machines also speak slower and lower.

A pause holds the speaker bit off for *n* × 64 ticks of the timer chip (0x156df).

### Machines

The presets are the calibration values the original would measure on each machine. They were worked out on paper from the published instruction timings of each processor for the two loops the calibration runs. **They have not been measured on hardware**, and bus wait states on port writes are ignored.

| `--machine` | Stands for | loop | overhead | extra | Pulse rate | Different pulse widths | Duty at silence | Duty range |
|---|---|---|---|---|---|---|---|---|
| `ideal` | no machine | 1 ns | 0 | 0 | 22,000 Hz | 255 | 50% | 0–100% |
| `486` | 486DX2, 66 MHz | 100 | 800 | 100 | 21,930 Hz | 204 | 49% | 1–99% |
| `386` (default) | 386DX, 33 MHz | 400 | 2,500 | 300 | 21,930 Hz | 108 | 46% | 2–96% |
| `at` | PC/AT, 286 at 8 MHz | 1,100 | 7,400 | 1,400 | 21,142 Hz | 36 | 37% | 5–86% |
| `xt` | PC/XT, 8088 at 4.77 MHz | 3,600 | 33,000 | 6,000 | 20,080 Hz | 4 | 20% | 20–42% |

The right-hand columns are computed by the ported table, not assumed. Your own values: `--timing LOOP,OVERHEAD[,EXTRA]` in nanoseconds.

What the table says about slow machines, if the estimates are near the truth:

- On an AT the 66 lowest sample values all give the shortest pulse: the negative half of the wave is cut off at about half of full scale.
- On an XT every value up to 128 gives the shortest pulse, and there are four pulse widths in all. Only positive peaks move the speaker. Speech still comes through because at volume 8 the signal is clipped to the limits most of the time, which is close to a one-bit signal already. That is a likely reason the voices are saved at volume 8.
- The XT also runs 9% slow (20,080 pulses a second instead of 22,000), so the voice is lower and slower.

### From the bit to audio

The on/off signal is turned into ordinary PCM without shortcuts that would add artefacts of their own:

1. The exact on-time inside each slot of 1/8 of an output sample is accumulated (so pulse edges are placed with nanosecond precision, not rounded to a grid).
2. A 385-tap low-pass (Blackman-windowed sinc, cut-off 15 kHz or lower for low output rates) removes the pulse rate and its harmonics; every eighth value is kept.
3. The result is 16-bit at `--rate` (default 48,000 Hz).

Checked in `check-pcspk` against the model's own arithmetic: a steady sample value comes out as a steady level equal to the pulse's duty cycle (largest error below 0.00001 over 70 combinations of machine, rate and value), what is left of the pulse rate is below −50 dB, and a pause lasts *n* × 64 timer ticks.

### The loudspeaker

`--speaker cone` (the default) passes the signal through three filters meant to resemble a small paper-cone speaker in a metal case: no bass below 300 Hz, a resonance of 6 dB at 2 kHz, little above 6 kHz. **This is invented to sound plausible. It is not measured from any speaker.** `--speaker none` gives the electrical signal with only the standing level removed.

The step between speech (average level near the middle) and a pause (speaker off) is real and is kept: it is the soft thump a PC speaker made around pauses.

## Trust, in one table

| Part | Standing |
|---|---|
| Pulse-width table, passes per sample | The original's, verified against it |
| Pulse period of 45,454 ns, pause of *n* × 64 ticks | The original's constants |
| Split of the overhead (30/70), the extra time | Read from the player's code, as instruction counts |
| Machine calibration values | Estimates from published instruction timings |
| Rendering to PCM | Exact for the model; checked numerically |
| Loudspeaker filter | Invented; judged right by ear from memory (see below) |

**Listening check.** the owner, who used the program in the 1990s, listened on 2026-10-09 to the `386`, `486` and `ideal` machines with and without the loudspeaker filter, and found that with the filter it sounded "just like back in 1995". That is a recollection, not a measurement. Of the `xt` preset he said later that the speaker "sounds like it is under a pile of snow": quiet and muffled, which is what the model predicts there (four pulse widths, a swing of 20 to 42% instead of 0 to 100%), but whether a real XT sounded like that is not known. After trying `--machine at` and `--machine xt --speaker none` he found it "louder and clearer"; which of the two he meant was not said.

Nobody has compared any of this with a recording of a real PC running MikroPuhe. If one turns up, `--timing` and the filter in `mp_pcspk_init` are the two places to adjust.

## Playing it

How the audio gets to a sound card, a file, or a machine you are not sitting at is in [playback.md](playback.md): ALSA device selection, pipes, the fallbacks, and what Linux offers for the physical PC speaker.
