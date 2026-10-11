# The original program: `PUHU.EXE`

Reference for the DOS program as a program: how it is built, how it is called, where things live in memory, and how it gets sound out of a PC. The speech algorithm itself is in [how-it-works.md](how-it-works.md), the voice files in [voice-format.md](voice-format.md).

Addresses are linear, with the image loaded at segment 0x1000 (the convention of Ghidra's DOS loader). File offset = address − 0x10000 + 0xC00. Data offsets are relative to the data segment, DS = 0x194a (linear 0x194a0 + offset, file offset 0xa0a0 + offset).

## The file

| | |
|---|---|
| Size | 47,816 bytes, SHA-256 in `original/SHA256SUMS` |
| Format | DOS MZ, 16-bit real mode, not packed; 3,072-byte header, 668 relocations, entry 0000:0000 |
| Compiler | Turbo C ("Turbo-C - Copyright (c) 1988 Borland Intl." at DS:0004), far code and far data, one code segment per source module |
| Banner | `MikroPuhe 1.0 Sarjanumero %-06lu Copyright 1990 Timehouse Oy` |
| Ships with | `JANNE.SMP`, `KRISTIAN.SMP`, `SOPHIA.SMP` |

Ghidra finds 262 procedures. It misses code reached only through `JMP CS:[BX+table]` switches; six such tables matter and were followed by hand (0x106e4, 0x12ec8, 0x13ce9, 0x1422d, 0x142f3, 0x14351).

## Module map

| Address range | Module | Content |
|---|---|---|
| 0x10000–0x1064f | C startup | Turbo C `c0`, long multiply and divide helpers (0x10412, 0x10578) |
| 0x10650–0x111ce | program | `main` (0x11047), option parser, sentence cutter, file reader |
| 0x111cf–0x11ebf | interface | the full-screen interface (0x11c57): main screen, settings screen, error box. See [interface.md](interface.md). |
| 0x11ec0–0x1262d | program support | voice selection (0x12398), error reporting, serial number (0x124ea), key check |
| 0x1262e–0x14c17 | speech library, C | text stage and word stage: `lib_speak` 0x126eb … `speak_phonemes` 0x13543, loader 0x14813 |
| 0x14c18–0x14c9f | small helpers | allocation wrappers, interrupt vector save and restore |
| 0x14ca0–0x14d4f | driver interface | signature, driver search (0x14cb8), call gate (0x14ce9) |
| 0x14d50–0x15166 | speech library, assembly | letter tables, pitch, unit lookup, cross-fade, volume |
| 0x15167–0x1594b | device layer | PC speaker: calibration, pulse-width table, player, timer interrupt, ring buffer |
| 0x1594c–0x163d6 | input field | a text-field editor of their own: drawing, scrolling, key handling (0x1606a) |
| 0x163d7–0x1949f | C library | Turbo C runtime: console, files, strings, memory |
| 0x194a0–0x1aec7 | data | initialised data. Ghidra labels it `CODE_90`. |
| 0x1aec8– | data | uninitialised data (zeroed at start) |

## Command line

```
PUHU                          full-screen interface
PUHU ?                        usage line
PUHU [options] text ...       speak the text
PUHU [options] :FILE          speak a text file
```

Options start with `/`, `+` or `-` and can be chained without spaces (`/KRI/NOP5`): `NOP KOR KRV VOI KNP ETP` with one digit, `JAN KRI SOP`, `EIT`. Their meaning is in [how-it-works.md](how-it-works.md#stage-1-the-program-cuts-sentences-and-finds-options). The same parser runs over the text while it is spoken.

Order of events in `main` (0x11047): options are read once to find the voice; the voice file is loaded and its saved settings become current; the options are read again and applied on top; then the text is spoken. Arguments after the options are joined with single spaces. A file is read in 16,000-byte pieces in text mode and ends at a 0x1A byte.

The banner and the echoed text are written straight to video memory, not through DOS.

While speaking, the program checks the keyboard between sentences: Esc quits, space repeats the sentence, F2 starts again from the beginning (strings at DS:0x0094).

Started without arguments, the program opens its full-screen, self-voicing interface. That is described in [interface.md](interface.md).

## Settings

Fourteen 16-bit words, in the same order in the voice file, in the program's copy (DS:0x1a98) and in the library's active block (DS:0x1b19). The table is in [voice-format.md](voice-format.md#settings-block). The library takes them through `set_settings` (0x14605), which limits the first four to 0–8 and turns the rest into flags.

## Globals worth knowing

| DS offset | Size | Meaning |
|---|---|---|
| 0x007f | word | last error code |
| 0x012b | byte | echo text on screen |
| 0x1a96 | word | current voice, 0–2 |
| 0x1a98 | 14 words | the program's settings |
| 0x1ac6 | word | ring buffer size given to the library (64000) |
| 0x1b08 | byte | digit-by-digit mode (after "puhelin") |
| 0x1b09, 0x1b50, 0x1b0d | far pointers | voice blocks 0, 1, 2 |
| 0x1b12 | byte | replacement for the letter `c` |
| 0x1b13 | word | sentence pitch level, 1–3 |
| 0x1b19 | 14 words | active settings |
| 0x1b47 | byte | bracket state: 0 off, 1 armed, 2 inside brackets |
| 0x1b54 | byte | decimal separator |
| 0x1b5b | 512 bytes | work buffer for one pitch period |
| 0x1d5b | 33 bytes | voice name |
| 0x1d7e | far pointer | 256-byte scratch string, allocated per speak call |
| 0x1d86 | | `setjmp` buffer: a key press aborts speech through `longjmp` |
| 0x1d9a | byte | nothing spoken yet in this speak call |
| 0x1d9f | word | samples per pitch period |
| 0x1da3–0x1da9, 0x1daf–0x1db1 | bytes | "forward this call to the resident driver" flags |
| 0x1db4 | byte | abort requested |
| 0x1dce, 0x1fde, 0x1fe0 | words | ring buffer write index, read index, size |
| 0x1dd0 | far pointer | ring buffer |
| 0x1dd4, 0x1dd8 | long | calibration: nanoseconds per delay-loop pass, per player pass |
| 0x1ddc | 257 words | pulse-width table |
| 0x1fe2 | long | samples and pause units played, for the clock correction |

## Output stage: the PC speaker

The library hands samples to a small device layer. In `PUHU.EXE` that layer drives the PC speaker by pulse-width modulation.

**Calibration** (0x15167), once per voice load. The timer chip is set to about 500 Hz and two loops are timed over 25 ticks each: an empty `loop` instruction and one pass of the player's inner loop. From this the program knows how many loop passes make a nanosecond interval on this machine. If one pass of the player takes 45,454 ns or more the machine cannot keep up and the program reports "Tämä laitteisto on liian hidas puhumiseen".

**Pulse-width table** (0x152b7). For each sample value the table holds how long the speaker bit stays on within a period of 2 × 22,727 ns = 45,454 ns, which is one sample at 22,000 Hz:

```
on-time = 22727 ns · (1 ± s / (64 + s/2))        s = |sample − 128|
```

So the duty cycle is 50% for silence and reaches 0% or 100% at full scale, with twice the slope for small signals.

**Player** (0x15616). For each sample: speaker bit on (`out 61h`), wait, bit off, wait the rest of the period. The timer interrupt (int 8) is taken over while speaking (handler at 0x15741); each interrupt plays what is in the buffer, up to the next pause.

**Ring buffer.** Samples are queued by `dev_write` (0x1589a) and consumed by the player. A byte 0 in the buffer is not a sample: it introduces a pause, followed by a 16-bit count. For that reason `dev_write` replaces every sample value 0 with 1.

<a name="pauses"></a>
**Pauses.** `dev_pause` (0x1551d) writes a pause shorter than 110 as that many silence samples, a longer one as a pause record. When the player meets a record it stops and programs the timer chip to wake it after `count × 64` timer ticks (0x156df). One timer tick is 1/1,193,182 s, so a pause unit is 53.6 µs, while a sample is 45.45 µs: on the PC a pause of *n* lasted 1.18 × *n* sample periods. If fewer than three bytes remain before the end of the ring buffer, the remaining one or two bytes are filled with silence first, so the buffer size (64000) has a tiny effect on the stream.

**Clock correction.** While speaking, the BIOS clock does not tick. At the end (0x157b4) the program adds `played / 1208` ticks to the BIOS counter, which is where the nominal rate shows up a second time: 1208 samples per 18.2 Hz tick is 21,993 Hz.

**What the port does with this.** The ring buffer, the timer handling and the calibration are gone: sample runs and pause counts go to an output sink. The sink is one of two emulated devices. `--device dac` writes the samples as they are, with pauses at 53.6 µs per unit unless `--nominal-pauses`. `--device pcspeaker` rebuilds the pulse train: the pulse-width table is ported and checked against the original, the calibration values come from a machine preset, and the one-bit signal is rendered to audio. See [pc-speaker.md](pc-speaker.md).

## The resident driver

The error messages mention a D/A converter card ("En löydä D/A-muunnin lisäkorttia", "D/A-muunnin lisäkortti on hajalla"), but `PUHU.EXE` contains no code for one. Instead it can hand its work to a resident driver:

- `find_driver` (0x14cb8) walks interrupt vectors 0x80–0xFF and compares the 24 bytes at handler address + 2 with `MikroPuhe-ohjainTIME` followed by AB B6 B2 BA (stored at 0x14ca0).
- Each library and device entry point first tests a flag byte and, if set, calls `driver_call` (0x14ce9) with a function number instead of doing the work itself.

| Number | Entry point |
|---|---|
| 1 | start the player (0x15717) |
| 2 | write samples (0x1589a) |
| 3 | initialise the device (0x1544a) |
| 4 | start a speak call (0x1548a) |
| 5 | end a speak call (0x154e1) |
| 6 | pause (0x1551d) |
| 7 | bytes pending (0x155e5) |
| 13 | speak text (0x126eb) |
| 14 | set settings (0x14605) |
| 15 | get settings (0x14761) |

Other function numbers may exist; only these were traced. The driver is not in the zip, so its side of the interface is unknown. That a driver could take over the whole library (13–15) suggests it also served other programs.

## Error codes and messages

`DS:0x007f` holds the last error. Values seen in the code:

| Value | Set at | Situation |
|---|---|---|
| 8 | 0x10efa, 0x14813 | out of memory |
| 13 | 0x14813 | voice file not readable as a voice |
| 19 | 0x14605 | a setting was out of range (and was replaced by 4) |
| 100 | 0x1546f | machine too slow |
| 104 | 0x127ef | no memory for the scratch string |
| 105 | 0x129db | speech was interrupted |
| 106 | 0x12720 | library not initialised |
| 107 | 0x12730 | buffer smaller than 1024 bytes |

The message table starts at DS:0x09a8: write-protected file, out of memory, file not found, machine too slow, D/A card not found, D/A card broken, "Ohjelmavirhe (1)", "Ohjelmavirhe (2)", "MikroPuheen näytetiedosto on sotkeutunut", "Päällekkäinen kutsu puhekirjastoon". Which code selects which message was not traced.

## Quirks and bugs, collected

| What | Where | Effect |
|---|---|---|
| Stale pitch bytes at a word-initial `j l m n v` | 0x14146 | a few periods with a pitch taken from uninitialised stack; details in [how-it-works.md](how-it-works.md#two-bugs-in-the-original-word-stage) |
| One pitch period too many in a diphthong glide | 0x13b2d | one period with a stale pitch byte |
| `1001` is spoken "tuhat" | 0x132fe | a last digit group equal to 1 adds nothing |
| A digit run swallows spaces | 0x128b4 | `1990 12:30` is one number string |
| Options are found inside words | 0x1099c | `-sop` in running text switches the voice |
| Characters in the exception list that can never be spoken | 0x12651 | `"`, `@`, `\`, `±` and a few more have names in the list but are class 4, so they are dropped before any lookup |
| Duplicate keys in the exception list | 0x12f79 | the second `atk` and `mm` entries are dead |
| An exception is not found when punctuation follows | 0x12f79 | the lookup gets the word with its punctuation and allows only for a final `.`: `esim,` or `klo:` stay unexpanded |
| Sentence-end pauses are switched off | 0x10ce0 | with the normal 64000-byte buffer, `.` `:` `?` `!` give no pause |
| Volume 8 clips | 0x15114 | shift left by 4; about 40% of the samples at the limits |
| A 126-character word without a space never finishes | 0x109f1 | seen in the emulator only; the loop is in the screen echo code and the cause was not established |

## Not known

- Whether anything checks the serial number. Nothing stopped the program in the emulator. (The number itself is known: 0x124ea reads it, inverted, from the four bytes after the string at DS:0x096d.)
- Settings words 11 and 12.
- The driver's side of the interface, and the D/A card.
