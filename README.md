# MikroPuhe 1.0, woken up

**A Finnish speech synthesiser from 1990, running again on Linux, macOS and Windows.**

MikroPuhe was a text-to-speech program for MS-DOS by Timehouse Oy: `PUHU.EXE` and three voices, Janne, Kristian and Sophia. It spoke through the PC speaker. This project is `puhu`, a new program in C written from a study of the original. It reads the original voice files and says the same things in the same voices, sample for sample.

```bash
puhu "hyvää päivää"
echo "Hei Michael, tämä on KITT" | puhu -v sophia
puhu                                   # the original's full-screen, talking interface
```

## At a glance

| | |
|---|---|
| **Version** | 1.2 (2026-10-11) |
| **Runs on** | Linux, macOS, Windows 10 and 11 |
| **Same speech as the original** | Checked against `PUHU.EXE` in a CPU emulator: the text handling matches call for call in all 89 test inputs, the samples are byte-identical in 86. The other three differ by 1 to 30 samples, where the original reads uninitialised memory. |
| **Same interface** | The full-screen program gives the same screen, colours, cursor and speech as the original for 26 hand-written and several hundred random key sequences. |
| **Same on every system** | The Linux, macOS and Windows builds give identical output for the same 156 inputs. |
| **Voices** | Janne, Kristian, Sophia (the originals), and Kone, a synthetic voice made for this project |

## About this copy

This is a copy of a private working repository, made to be shown. It does not speak by itself.

| Left out | What to do |
|---|---|
| **The original program and voices.** `PUHU.EXE` and the three `.SMP` files are Timehouse Oy's. | Put your own copies into [`original/files/`](original/files/README.md). |
| **Two generated C headers** (`port/src/tables.h`, `port/src/ui_tables.h`) | The build makes them from your `PUHU.EXE`. It needs Python 3 for that, once. |
| **Listings of the original's code and data** (`evidence/functions/`, `evidence/disasm-*.txt`, `evidence/smpdump-*.txt`), which the documents cite | `tools/fn_extract.py`, `tools/mzdis.py` and `tools/smpdump.py` produce them from the original files. |
| **Names.** The person the notes call "the owner" did the listening, the deciding and the asking. | |

What is here and is this project's own: `voices/KONE.SMP`, a synthetic voice computed by `tools/make_synth_voice.py`.

## Quick start

You need a C compiler and `make`.

```bash
make -C port                 # builds port/build/puhu
make -C port install         # optional: a link in ~/.local/bin, so that "puhu" works anywhere
make -C port windows         # optional: cross-compile puhu.exe (needs MinGW-w64)
```

| To | Type |
|---|---|
| speak a sentence | `puhu "hyvää päivää"` |
| choose voice, speed and volume | `puhu -v sophia -s 4 -V 4 "hyvää päivää"` |
| speak what another program prints | `echo "Hei Michael, tämä on KITT" \| puhu -v sophia` |
| write a file instead of playing | `puhu -o hei.wav "hei"`, or `.mp3` (through lame or ffmpeg) |
| the same by redirection | `puhu "hei" > hei.wav` |
| read a text file | `puhu -f teksti.txt -o teksti.wav` |
| hear it as on a PC speaker of a 286 | `puhu -d pcspeaker --machine at "hyvää päivää"` |
| use the synthetic voice | `puhu -v kone "olen puhuva kone"` |
| open the full-screen interface | `puhu` |
| see every option | `puhu --help` |

The original voices are saved at volume 8, which clips heavily, and Janne and Sophia at speed 0, the slowest. `-s 4 -V 4` sounds better.

**Sound from a machine reached over SSH.** An SSH session has no sound device. `port/puhu-listen`, run on the machine you sit at, opens a tunnel and plays what `puhu` on the remote machine says. Step by step: [`docs/playback.md`](docs/playback.md#3-play-by-itself-from-the-remote-terminal-through-an-ssh-tunnel).

## Things to try

```bash
# three voices in one sentence: the original's options work inside the text
puhu -s 4 -V 4 "/JAN minä olen Janne. /KRI minä olen Kristian. /SOP ja minä olen Sophia."

# speed and pitch changed on the way: /NOP is speed, /KOR is pitch, 0 to 8
puhu -s 4 -V 4 "/NOP8 nopeasti /NOP0 hitaasti /KOR8 korkealta /KOR0 matalalta"

# numbers, abbreviations and signs are read out as words
puhu -s 4 -V 4 "kello on 12.45 ja lämpötila on -3 astetta"
puhu -s 4 -V 4 "esim. 3 kg, 15 % ja 1990"

# the same greeting on five machines, from a perfect speaker down to a PC/XT
for m in ideal 486 386 at xt; do puhu -s 4 -V 4 -d pcspeaker --machine $m "hyvää päivää"; done

# the voice that is not a recording
puhu -v kone "olen puhuva kone"
```

## What it looks like

Started with nothing to say, `puhu` opens the program the original opened: a full-screen interface that greets you, reads its help aloud and announces every setting as you move over it.

```
         MikroPuhe 1.0 Sarjanumero ···    Copyright 1990 Timehouse Oy

                           ┌──────────────────────────────────────────────┐
     Anna puhuttava lause: │hyvää päivää                                  │
                           └──────────────────────────────────────────────┘




    F1 Apu   F2 Puhearvot   ↑ ↓ Lauseiden selaus   ◄──┘ Puhu   Esc Lopetus
```

It is the original's code path, ported routine by routine, and it draws the same 2,000 characters in the same colours ([`docs/interface.md`](docs/interface.md)).

## The PC speaker, rebuilt

The original had no sound card to talk to. It drove the PC speaker, a part that can only be switched on or off, by switching it about 22,000 times a second and varying how long it stayed on each time. To get those times right on any machine it measured, at every start, how long one pass of its own delay loop took, and built a table of 256 pulse widths from that.

A modern machine has no such speaker, so this project builds one in software, and it builds the machine around it too:

```bash
puhu -d pcspeaker "hyvää päivää"                      # a 386 with a small loudspeaker
puhu -d pcspeaker --machine xt "hyvää päivää"         # a PC/XT
puhu -d pcspeaker --machine 486 --speaker none ...    # the bare electrical signal of a 486
puhu -d pcspeaker --timing 250,1800,200 ...           # a machine of your own, in nanoseconds
```

| `--machine` | The machine | Delay loop pass | Pulse widths it can make |
|---|---|---|---|
| `ideal` | none: exact widths | 1 ns | 255 |
| `486` | 486DX2, 66 MHz | 100 ns | 204 |
| `386` (default) | 386DX, 33 MHz | 400 ns | 108 |
| `at` | PC/AT, 286 at 8 MHz | 1,100 ns | 36 |
| `xt` | PC/XT, 8088 at 4.77 MHz | 3,600 ns | 4 |

The slower the machine, the coarser its delay loop, and the fewer different pulse widths are left for the 256 sample values. An XT has four, which is why it sounds as if the speaker were lying under a pile of snow.

How it is put together:

1. **The original's own arithmetic.** The pulse-width table and the player's loop counts are ported from the program and give the same numbers as the original for twelve different calibrations.
2. **A machine.** Each preset says what the original's calibration would have measured on that processor, worked out from the processor's published instruction timings.
3. **The speaker's signal.** The on and off times are rendered as the 1-bit pulse train they were, at eight times the output rate, and filtered down.
4. **A loudspeaker.** `--speaker cone` adds a small paper cone in a metal case: no bass, a peak around 2 kHz, little above 6 kHz. `--speaker none` leaves the electrical signal.

Step 1 is checked against the original. Steps 2 to 4 are a model: estimates made by reasoning, not measurements of real hardware. To the ear of someone who used the program in the 1990s, the 386 and 486 with the cone sounded "just like back in 1995". More: [`docs/pc-speaker.md`](docs/pc-speaker.md).

## The voices

| Voice | File | Pitch | What |
|---|---|---|---|
| Janne | `JANNE.SMP` | 98 Hz | original, recorded |
| Kristian | `KRISTIAN.SMP` | 92 Hz | original, recorded |
| Sophia | `SOPHIA.SMP` | 210 Hz | original, recorded |
| Kone | `voices/KONE.SMP` | 110 Hz | new: nothing in it is recorded, a script computes every sound. The original program accepts it too. Nobody has listened to it critically yet. |

A voice file can be taken apart, edited and put together again with `tools/smptool.py` ([`docs/voice-format.md`](docs/voice-format.md)).

## The systems

| System | Sound goes through | State |
|---|---|---|
| Linux | ALSA, or a player program (`pw-play`, `aplay`, `paplay`, `ffplay`) | the reference build, checked against the original |
| macOS | `afplay`, part of the system | identical output to Linux; used by hand |
| Windows 10, 11 | the system's own sound output | cross-compiled; identical output to Linux, on Windows 11 and under Wine; used by hand |

How a build is taken to another system and checked there: [`docs/porting.md`](docs/porting.md).

## How it works, in short

- Finnish spelling is nearly phonemic, so the program speaks the letters. It first turns numbers and abbreviations into words and rewrites a few foreign letters.
- Vowels and `j l m n v` are each one recorded pitch period, repeated, cross-faded into the next sound, and shortened or padded to change the pitch. Stops and `f h r s` are short recorded noises.
- The output is 8-bit samples at a nominal 22,000 Hz, which the original played on the PC speaker by pulse-width modulation with calibrated delay loops.
- A voice file holds the recorded units, 32 duration and pitch parameters, three pitch contour curves, and a list of 149 spelling exceptions.

The long version: [`docs/how-it-works.md`](docs/how-it-works.md).

## How it was made

There was no source code to work from. Everything here was worked out from the program file itself, in three days, by an AI agent (Claude) using reverse-engineering tools, with a person who knew the program from the 1990s listening, testing and deciding.

**REA** ([Reverse Engineer Anything](https://github.com/morluto/rea), open source) was the way into the binary. It puts reverse-engineering engines behind one command line and one tool interface for agents; here it drove Ghidra 12.1.4.

| What REA was used for | What came out |
|---|---|
| `rea inspect` on `PUHU.EXE` | the format and compiler: a DOS MZ program, 16-bit real mode, Turbo C 2.0 |
| lists of procedures and strings | 262 procedures, and the program's texts, which show where the interface, the number reader and the voice loader are |
| `rea instructions` for one procedure at a time | the exact machine code of each routine, which is what the C port was written from |
| callers and callees | the shape of the program: text stage, word stage, sample generator, PC-speaker player |
| `rea function`, with Ghidra's pseudocode | a first reading of each routine |

All 262 procedures were analysed once, in parallel, and saved as one text file each. After that, reading the program was searching text.

Where it had to be helped, and how:

- **The pseudocode cannot be trusted alone.** Ghidra treats the program's data as constant and deletes branches it believes dead: the central routine decompiled to four lines. The instruction lists are the truth; the pseudocode is a hint.
- **Jump tables hide code.** Several hundred bytes inside three routines were never disassembled. A second, simpler disassembler (capstone, `tools/mzdis.py`) covers those ranges.
- **Reading is not proof.** The decisive tool was an oracle: the unmodified `PUHU.EXE` running in a CPU emulator (Unicorn, `tools/puhu_emu.py`), cut where it hands samples to the speaker. Every routine of the port was compared with what the original does for the same input, down to the bytes of uninitialised memory it happens to read.

The route step by step, what worked and what to watch for: [`docs/method.md`](docs/method.md). How the port is checked: [`docs/verification.md`](docs/verification.md).

## Documentation

| Read this | For |
|---|---|
| [`docs/how-it-works.md`](docs/how-it-works.md) | The synthesiser, stage by stage: sentences, character classes, exceptions, numbers, how a word becomes pitch periods and samples |
| [`docs/voice-format.md`](docs/voice-format.md) | The `.SMP` voice file, field by field; how to edit a voice or build one; how Kone is made |
| [`docs/original-program.md`](docs/original-program.md) | `PUHU.EXE` as a program: modules, command line, PC-speaker output, driver interface, error codes, quirks and bugs |
| [`docs/interface.md`](docs/interface.md) | The full-screen, self-voicing interface: screens, keys, how the port follows the original |
| [`docs/playback.md`](docs/playback.md) | Getting text in and sound out: pipes, files, sound cards, why you may hear nothing, hearing a remote machine |
| [`docs/pc-speaker.md`](docs/pc-speaker.md) | The virtual PC speaker: what is the original's, what is a model, how far to trust it |
| [`docs/macos.md`](docs/macos.md), [`docs/windows.md`](docs/windows.md) | `puhu` on macOS and on Windows: what was checked, what differs, how to set it up |
| [`docs/porting.md`](docs/porting.md) | Taking `puhu` to another system: the methods, the 156-case comparison, the traps |
| [`docs/verification.md`](docs/verification.md) | How the port is checked against the original, the results, and where the claim stops |
| [`docs/method.md`](docs/method.md) | How the reverse engineering was done |
| [`port/README.md`](port/README.md) | The port: build, all options, source layout, the core's C interface |
| [`tools/README.md`](tools/README.md) | The emulator harness and the other scripts |
| [`notes/FUNCTIONS.md`](notes/FUNCTIONS.md) | Map of the original's functions by address, and which C function follows each |
| [`notes/NOTES.md`](notes/NOTES.md) | The dated log: what was run, observed, inferred and left unknown |
| [`port/PLAN.md`](port/PLAN.md) | What was planned, what is done, what is left |
| [`AGENTS.md`](AGENTS.md) | For coding agents and people about to change something: what the project promises, what never to do, which checks to run |

## Layout

```
original/      the files as received, with SHA256SUMS. Never modified.
  files/         PUHU.EXE, JANNE.SMP, KRISTIAN.SMP, SOPHIA.SMP
port/          the C port, its tests, and puhu-emu (the original in the emulator, same options)
voices/        KONE.SMP, the synthetic voice
tools/         the emulator harness and scripts
docs/          the documentation listed above
notes/         NOTES.md (log), FUNCTIONS.md (function map)
evidence/      saved tool output that the notes cite
```

In this copy `original/files/` holds only a note on what to put there, and `evidence/` only `reference/MANIFEST.tsv` and `windows/`.

The original files, to check your copies against (`cd original && sha256sum -c SHA256SUMS`):

| File | Bytes | What |
|---|---|---|
| `PUHU.EXE` | 47,816 | the program: DOS, 16-bit real mode, Turbo C |
| `JANNE.SMP` | 39,489 | voice, pitch period 225 samples |
| `KRISTIAN.SMP` | 39,540 | voice, 240 samples |
| `SOPHIA.SMP` | 41,566 | voice, 105 samples |

## Still open

- Whether the serial number or anything else gates the original. Nothing stopped it in the emulator.
- 1,976 bytes in each voice file that the program never reads.
- What a real PC speaker did to the sound.
- What Kone sounds like to a critical ear, and tuning it.
- More in [`port/PLAN.md`](port/PLAN.md).

## Rights

Timehouse Oy holds the copyright (1990) on MikroPuhe: the program `PUHU.EXE` and the voices Janne, Kristian and Sophia. None of those files is in this repository, and it is not a way to get them.

What is here was written to understand how a speech synthesiser of 1990 worked and to keep it running on today's machines: a port made from a study of the program, the documentation of that study, and one voice (Kone) that is entirely this project's own. The documents quote texts of the program where they describe it, and the build reads the program's number words and interface texts from your own copy of `PUHU.EXE`.

This repository carries no licence. What was written for this project is shown for study, and for the people who remember the program; it is not offered for reuse or redistribution. It is not a product of Timehouse Oy and has no connection with it. If you speak for the right holder and want something here changed or taken down, open an issue in this repository and it will be dealt with promptly.
