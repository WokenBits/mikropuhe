# The MikroPuhe voice file (`.SMP`)

A voice is one file of about 40 KB: `JANNE.SMP`, `KRISTIAN.SMP`, `SOPHIA.SMP`. The format below is what the loader in `PUHU.EXE` reads (0x14813) and what the synthesiser then uses. It is implemented in [`port/src/voice.c`](../port/src/voice.c) and can be checked on any file with

```bash
tools/smpdump.py original/files/JANNE.SMP [--units] [--exceptions] [--wav DIR]
```

which prints every field and ends with a count of explained bytes. Full dumps of the three files are in `evidence/smpdump-*.txt`.

All numbers are little-endian. Text is in code page 437 (`ä` = 0x84, `ö` = 0x94, `å` = 0x86).

## Container

| Offset | Size | Content |
|---|---|---|
| 0 | 51 | Signature: `MikroPuhe - näytetiedosto`, CR LF, `Timehouse Oy 1990`, CR LF, 0x1A, `TIME`. The 0x1A makes DOS `TYPE` stop after the two readable lines. |
| 51 | 1 | Length L of the voice name |
| 52 | L | Voice name (the program keeps at most 32 bytes) |
| 52+L | 46 | Settings block: 14 words, then 18 bytes 0xFF |
| 98+L | 12 | Three 32-bit lengths: block 0, block 1, block 2 |
| 110+L | len0 | Block 0: parameters and pitch contours |
| | len1 (+1 if odd) | Block 1: exception list |
| | len2 (+1 if odd) | Block 2: waveform units |

Each block is stored padded to an even length. The program allocates the three blocks as one piece of memory and keeps a pointer to each.

| | Janne | Kristian | Sophia |
|---|---|---|---|
| File size | 39,489 | 39,540 | 41,566 |
| Block 0 | 3000 | 3000 | 3000 |
| Block 1 | 1825 | 1825 | 1825 |
| Block 2 | 34,544 | 34,596 | 36,624 |
| Bytes after the last block | 4 (`0D 0A 0D 0A`) | 0 | 0 |

## Settings block

The speech settings as last saved from the program's full-screen interface (F2). They become the current settings when the voice is loaded. (`puhu` saves into a copy of the voice under `~/.local/share/mikropuhe/`, not into the shipped file; see [interface.md](interface.md#the-settings-screen-f2).)

| Word | Setting | Janne | Kristian | Sophia |
|---|---|---|---|---|
| 0 | speed, 0 slowest … 8 | 0 | 3 | 0 |
| 1 | pitch, 4 neutral | 4 | 5 | 0 |
| 2 | pitch variation | 0 | 8 | 0 |
| 3 | volume, 4 unity | 8 | 8 | 8 |
| 4 | consonant emphasis | 1 | 0 | 1 |
| 5 | first-syllable stress | 1 | 1 | 1 |
| 6 | a lone underscore is spoken | 0 | 0 | 0 |
| 7 | decimal separator is `.` instead of `,` | 0 | 0 | 0 |
| 8 | bracketed text is spoken quieter | 1 | 0 | 1 |
| 9 | letter `c` is spoken as `k` instead of `s` | 0 | 1 | 0 |
| 10 | hook the keyboard while speaking | 1 | 1 | 1 |
| 11 | not identified | 1 | 1 | 1 |
| 12 | not identified | 0 | 0 | 0 |
| 13 | sentence-end pauses in the word stage; overwritten by the program at run time | 1 | 1 | 1 |

Words 0–3 are limited to 0–8 when loaded (anything else becomes 4); words 4–13 are treated as flags.

## Block 0: parameters and pitch contours

3000 bytes. Words at 0x00–0x3F are parameters; the names are mine, from how the code uses them.

| Offset | Parameter | Janne | Kristian | Sophia |
|---|---|---|---|---|
| 0x00 | stop slot in samples: closure pause = slot − burst length | 2200 | 2200 | 2200 |
| 0x02 | extra closure for a doubled stop, samples | 3000 | 3000 | 3000 |
| 0x04, 0x06, 0x08 | repeats of the noise unit for `h`, `r`, `s` | 1, 2, 1 | 1, 2, 1 | 1, 2, 1 |
| 0x0A, 0x0C, 0x0E | the same for `hh`, `rr`, `ss` | 3, 5, 3 | 3, 5, 3 | 3, 5, 3 |
| 0x10, 0x12, 0x14 | periods of a vowel at stress level 0, 1, 2 | 9, 8, 7 | 8, 7, 6 | 19, 15, 13 |
| 0x16, 0x18, 0x1A | glide periods of a diphthong, by stress level | 4, 3, 3 | 5, 4, 4 | 11, 9, 9 |
| 0x1C, 0x1E, 0x20 | periods of a diphthong's second vowel, by stress level | 8, 7, 6 | 6, 5, 4 | 13, 11, 9 |
| 0x22 | glide periods between `j l m n v` and a vowel | 3 | 3 | 6 |
| 0x24 | periods of `j l m n v` between two vowels | 1 | 1 | 2 |
| 0x26 | … before `b d f g k p t` | 7 | 7 | 15 |
| 0x28 | … at the end of a word | 10 | 9 | 21 |
| 0x2A | extra periods for a doubled `j l m n v` | 10 | 11 | 30 |
| 0x2C | pitch byte a `j l m n v` starts with when no vowel precedes it | −5 | −5 | −5 |
| 0x2E | pitch step per period inside `j l m n v` | −2 | −2 | −2 |
| 0x30, 0x32, 0x34 | pitch offset at sentence level 1, 2, 3 | 5, 0, −15 | 5, 0, −15 | 5, 0, −15 |
| 0x36 | samples per pitch period | 225 | 240 | 105 |
| 0x38 | periods of `j l m n v` in other positions | 5 | 5 | 11 |
| 0x3A, 0x3C, 0x3E | extra periods of a long vowel, by stress level | 3, 2, 2 | 5, 4, 4 | 5, 4, 4 |

Sophia's counts are about twice the others' because her period is half as long.

| Offset | Content |
|---|---|
| 0x040–0x0FF | zero |
| 0x100–0x1FF | pitch contour template for stress level 0: 256 signed bytes, −17 … +18 |
| 0x200–0x2FF | template for stress level 1: −34 … −11 |
| 0x300–0x3FF | template for stress level 2: −50 … −36 |
| 0x400–0xBB7 | 1976 bytes the program never reads |

A template is the pitch course of one vowel run from start to end. It is resampled to the number of periods in the run; each value then becomes a number of samples cut from or added to a period (see [how-it-works.md](how-it-works.md#pitch)). The three templates are the same in all three voices.

**The unread 1976 bytes.** They are identical in the three files and look like more curves in 256-byte steps: unsigned values rising from about 80 to 176 and falling again, with zeros at the end. No instruction in `PUHU.EXE` reads block 0 beyond offset 0x3FF (the block pointer is used in four functions, all ported). What they were for is unknown; a guess is contours left over from the voice editor or from an earlier version.

## Block 1: exception list

| Offset | Size | Content |
|---|---|---|
| 0 | 2 | number of entries (149) |
| 2 | 2 | zero |
| 4 | | entries |

Each entry is two byte strings, each with a length byte in front: the key, then the replacement.

```
02 'n' 's'   0C 'n' 'i' 'i' 'n' '&' 's' 'a' 'n' 'o' 't' 't' 'u'
```

Lookup (0x12f79) is a linear search for a key with the same length and bytes as the word. If the word ends in `.` the dot is ignored for the comparison and put back after the replacement. The list is searched in order and the first match wins, so of two entries with the same key (`atk`, `mm`) only the first is ever used.

The list is the same in all three voices: all 1825 bytes parse as exactly 149 entries. It holds

- abbreviations: `esim` → `esimerkiksi`, `jne` → `ja-niin-edelleen`, `klo` → `kello`, `puh` → `puhelin`, `tj` → `toimitusjohtaja`;
- units: `mk` → `markkaa`, `kg` → `kiloa`, `km` → `kilometriä`, `kpl` → `kappaletta`;
- weekdays: `ma` → `maanantai` … `su` → `sunnuntai`;
- names that are not pronounced as written: `timehouse` → `taim#haus`, `macintosh` → `mä#kintoss`, `ibm` → `iii-bee-ämmm`, `bmw` → `bemari`, `tre` → `manse`;
- the name of every letter: `a` → `aa`, `f` → `äff`, `w` → `kaksoisvee`, `å` → `ruotsalainen&&oo`;
- the name of every special character: `%` → `prosentti`, `(` → `avaussulku`, `§` → `pykälä`, `=` → `on_yhtäkuin`.

In a replacement, `_`, `#`, `-` and `&` are instructions to the word stage (unstressed, stressed, unstressed, short pause).

## Block 2: waveform units

| Offset | Size | Content |
|---|---|---|
| 0 | 16 | four 32-bit offsets, from the start of the block, to tables 1–4; 0 = table absent |

A **table** is a sequence of units, addressed by number from 1. A **unit** is:

```
word   count of variants
count times:
    word   length in bytes
    bytes  samples, 8-bit unsigned, 128 = silence; padded to an even length
```

There is no unit count: a table ends where the next one begins (or the block ends). To find variant *v* of unit *u* the program walks the table from its start (0x14e1a). Asking for a variant beyond a unit's count gives its first variant.

| Table | Units | Variants | Used for |
|---|---|---|---|
| 1 | 1 `b`, 2 `d`, 3 `f`, 4 `g`, 5 `k`, 6 `p`, 7 `t` | `d k p t`: 9 each; `b f g`: 1 | The burst of a stop. Variant 1 when no vowel follows, 2–9 before `a e i o u y ä ö`. Lengths 130 to 2800 samples. |
| 2 | 1 `h`, 2 `r`, 3 `s` | 1 each | Noise, 800 to 2400 samples, played one or more times |
| 3 | 1–8 the vowels `a e i o u y ä ö`, 9 `j`, 10 `l`, 11 `m`, 12 `n`, 13 `v`, 14 `ng` | vowels 2, the rest 1 | One pitch period, exactly as long as the voice's period. A vowel's variant 2 is used directly after `m` or `n`. |
| 4 | 1 `ff` | | Absent in all three files. If present it would be used for a doubled `f`. |

In all three files every table ends exactly where the next begins: 7 units and 39 variants in table 1, 3 and 3 in table 2, 14 and 22 in table 3.

To listen to the raw units:

```bash
tools/smpdump.py original/files/JANNE.SMP --wav /tmp/janne-units
```

A single pitch period is a 5 to 11 ms click; loop it in an audio editor to hear the vowel.

## Editing a voice, or making one

`tools/smptool.py` takes a voice file apart into files you can edit, and builds a voice file from them:

```bash
tools/smptool.py unpack original/files/JANNE.SMP /tmp/janne
#   /tmp/janne/voice.ini        name, the 14 saved settings, the 32 parameters
#   /tmp/janne/exceptions.tsv   key <TAB> replacement, one per line
#   /tmp/janne/contours.tsv     the three pitch contours, one column each
#   /tmp/janne/units/*.wav      every recorded unit (8-bit mono, 22000 Hz)
#   /tmp/janne/raw/             bytes the program never reads, kept as they are

$EDITOR /tmp/janne/exceptions.tsv        # add:  wifi<TAB>vai_fai
tools/smptool.py pack /tmp/janne /tmp/JANNE2.SMP
puhu -v /tmp/JANNE2.SMP "wifi toimii"
```

Unpacking and packing without changes gives back the identical file, for all three voices (`smptool.py roundtrip FILE`).

What can be changed:

- **The exception list.** Add, remove or reorder lines. A key is matched against a whole word in lower case; the first matching line wins. In a replacement, `_` `#` `-` `&` are stress and pause marks (see [how-it-works.md](how-it-works.md#markers)). Key and replacement are at most 255 bytes each.
- **The saved settings and the voice's name**, in `voice.ini`.
- **The parameters and the pitch contours**: the rhythm and intonation of the voice.
- **The recordings.** Replace a WAV in `units/` by another 8-bit mono one. A unit of table 3 (vowels and `j l m n v ng`) must be exactly one pitch period long, the `period_samples` of `voice.ini`; `pack` refuses anything else. The file names carry the table, unit and variant numbers, which decide where a recording goes.

A whole new voice is the same directory with someone else's recordings in it: 14 single pitch periods (the 8 vowels in two variants each), bursts for `b d f g k p t`, and noise for `h r s`.

Checked: an edited Janne (another name, other saved settings, five added exceptions) was loaded by the original `PUHU.EXE` in the emulator, which spoke the new exceptions and used the new settings, and `puhu -v` gave the identical sample stream from the same file. The original copies of the voices in `original/files/` are never modified; work on copies.

One thing to know about exceptions: the lookup sees the word with whatever punctuation is attached to it, and only a final full stop is allowed for. `google` matches "google" and "google." but not "google,".

## Kone: a voice with nothing recorded in it

`voices/KONE.SMP` is a fourth voice, made for this project on 2026-10-11. No recording is in it: `tools/make_synth_voice.py` computes every sound unit, and its parameters, pitch contours and exception list were written for it. Only the file format is the original's, including the 51 bytes of signature that the loader insists on.

```bash
puhu -v kone "hyvää päivää, olen puhuva kone"
make -C port kone        # generate it again: voices/KONE.SMP comes out byte for byte the same
```

`-v NAME` finds `NAME.SMP` among your own voices, in the voices directory, or in this repository's `voices/`. On Windows it lies next to `puhu.exe`.

| Sound | How it is computed |
|---|---|
| the eight vowels | one pitch period of 200 samples (110 Hz): the settled answer of four resonators, set to the vowel's formant frequencies, to one pulse per period |
| a vowel after `m` or `n` (variant 2) | the same with a fifth of a nasal hum mixed in |
| `j l m n v ng` | the same method with their own formants, quieter and duller |
| `h`, `s`, `f` | noise through resonators: `h` at a neutral vowel's formants, `s` around 5 and 8 kHz, `f` broad |
| `r` | four periods of a neutral vowel with a dip in loudness: one tap of the tongue, played twice for `r`, four times for `rr` |
| `k p t` | a burst of noise that dies away in 2 to 5 ms, coloured by the vowel that follows: `p` low, `t` high, `k` at the vowel's second formant and longer |
| `b d g` | a low hum (the voice behind closed lips), then a weaker burst |

The vowel formants are textbook values for Finnish; everything else (bandwidths, loudness of each sound, burst shapes, durations) was chosen by reasoning, not by ear.

The pitch contours are flatter than those of the three original voices, on purpose: a gentle fall of 10 samples per run, the stressed levels higher. The exception list has 92 entries: the Finnish names of the 29 letters and of 33 signs, 21 abbreviations, units and weekdays, and 9 names (`windows` → `vindous`, `github` → `git-hab`).

**Checked:**

- `smptool.py roundtrip` gives the identical file; `smpdump.py` explains all 34,678 bytes.
- The original `PUHU.EXE` in the emulator, given this file in place of a voice, speaks it: four sentences, among them numbers and doubled stops, come out byte for byte the same as from `puhu -v kone`.
- The resonances of the eight vowel periods, measured from the generated files, are where they were asked for, within one harmonic (110 Hz).

**Not checked: what it sounds like.** Nobody has listened to it yet. Whether the consonants can be told apart, whether the bursts are too loud or too soft, and whether the timing is natural needs ears, and then changes to the numbers in the script. Samples next to the same sentences from Janne: `port/demo/kone-*.wav`, `port/demo/janne-*.wav` (after `make`; the folder is not in git).

## How much is explained

| | Janne | Kristian | Sophia |
|---|---|---|---|
| Bytes in the file | 39,489 | 39,540 | 41,566 |
| Explained by the format above | 37,509 | 37,564 | 39,590 |
| Unread part of block 0 | 1976 | 1976 | 1976 |
| Bytes after the last block | 4 | 0 | 0 |
