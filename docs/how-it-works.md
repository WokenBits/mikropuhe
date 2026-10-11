# How MikroPuhe 1.0 turns text into speech

This describes the synthesiser inside `PUHU.EXE` (Timehouse Oy, 1990) as recovered from its machine code. Everything here was read from the disassembly and then confirmed by the C port producing the same output as the original (see [verification.md](verification.md)). Addresses in parentheses are linear addresses in the program image loaded at segment 0x1000; the function names are the ones used in [`notes/FUNCTIONS.md`](../notes/FUNCTIONS.md) and in the C source under [`port/src/`](../port/src/).

The voice file layout is in [voice-format.md](voice-format.md). The DOS program around the synthesiser (command line, PC-speaker output, driver interface) is in [original-program.md](original-program.md).

## The idea in one paragraph

Finnish is written almost exactly as it is pronounced, so MikroPuhe has no phoneme dictionary. It cleans the text up (numbers and abbreviations become words, a few foreign letters are rewritten) and then speaks the letters. Vowels and the consonants `j l m n v` are each stored in the voice file as **one pitch period** of a recorded voice, 105 to 240 samples long. A sound is made by repeating its period; one sound glides into the next by cross-fading two periods; pitch is changed by cutting samples off the end of each period or adding silence to it. The other consonants are short recorded noises that are played as they are. The result is an 8-bit sample stream at 22 kHz.

## The pipeline

```
text ──► sentences ──► runs of one character class ──► words in spelling ──► units + durations + pitch ──► samples
        speak_text     mp_speak (0x126eb)              speak_word (0x12a4f)   speak_phonemes (0x13543)     synth_voiced (0x14f82)
        (0x10ce0)                                      speak_number (0x12c74)                              synth_plain  (0x143f2)
                                                                                                           plain_unit   (0x14384)
```

A real example, traced from the original with `tools/puhu_emu.py --calls`. The input

```
Hei! Puhelin 010 123, hinta 12,50 mk eli 3. erä (wc).
```

reaches the word stage as three speak calls with these words:

| Speak call | Words handed to the word stage |
|---|---|
| `Hei! ` | `hei!` |
| `Puhelin 010 123, hinta 12,50 mk eli 3. ` | `puhelin` `nolla` `yksi` `nolla` `yksi` `kaksi` `kolme` (pause 4000) `hinta` `kaksitoista _pilkku viisinolla` `markkaa` `eli` `kolmas` |
| `erä (wc).` | `erä` `avaussulku` `kaksoisvee` `see` `loppusulku` `piste` |

Things to notice: after the word "puhelin" digits are read one by one; `12,50` becomes a number with a decimal part; `mk` comes from the exception list; `3.` is an ordinal; a word without vowels (`wc`) is spelled out, and here the brackets and the full stop are named too because they are attached to it. Also: "3. " ended a sentence, because the cutter only looks for `.` followed by a space.

## Stage 1: the program cuts sentences and finds options

`speak_text` (0x10ce0) cuts the text at `.`, `:`, `?` or `!` when the next character is a space or a line end. Each piece, including its punctuation and the space after it, goes to `speak_piece` (0x1099c).

`speak_piece` looks at every position of the piece for an option: `/`, `+` or `-`, a three-letter code, and for the first six codes one digit (parser at 0x10669). Text before the option is spoken, the option is applied, and the scan continues after it. The scan does not care about word boundaries.

| Code | Finnish name in the program | Meaning | Values |
|---|---|---|---|
| `NOP` | nopeus | speed | 0 slowest … 8 fastest |
| `KOR` | korkeus | pitch | 0 … 8, 4 neutral |
| `KRV` | korkeuden vaihtelu | pitch variation | 0 … 8 |
| `VOI` | voimakkuus | volume | 0 … 8, 4 is unity gain |
| `KNP` | konsonanttien painotus | consonant emphasis | 0 or 1 |
| `ETP` | ensimmäisen tavun painotus | first-syllable stress | 0 or 1 |
| `JAN` `KRI` `SOP` | | voice Janne, Kristian, Sophia | |
| `EIT` | | no echo of the text on screen | |

A digit outside 0–8 is replaced by 4 (0x145e8). Loading a voice makes that voice's saved settings current (0x12398).

Before a piece is spoken, line ends inside it are replaced by spaces (0x108ef).

## Stage 2: character classes and runs

`mp_speak` (0x126eb) reads the piece one character at a time. Each character is lower-cased (A–Z, and `Å Ä Ö Ü`) and classified (0x12651):

| Class | Characters |
|---|---|
| 1 letter | `a`–`z`, `å ä ö ü`, `_`, `#` |
| 2 digit | `0`–`9` |
| 3 special | ``! $ % & / ( ) [ ] { } = ? + é φ æ § - £ * ' . , ; : < > ° ^ ² ⁿ √`` |
| 4 other | everything else, including space |

Characters are collected into a run. What happens when the class changes:

| Run so far | Next character | Result |
|---|---|---|
| letters | special | the special joins the run (`päivää,` stays one run) |
| letters | digit or other | run → `speak_word` |
| digits | space or special | joins the run (the space is dropped) |
| digits | letter or other | run → `speak_number` |
| specials | letter or digit | the run continues and becomes a letter or digit run (`(suluissa)`) |
| specials | other | each character is looked up in the exception list and spoken by name |

A run is cut after 100 characters. Class 4 characters are never stored.

Because a space joins a digit run, `1990 12:30 50%` reaches the number handler as one string. That is the original's behaviour.

## Stage 3a: words

`speak_word` (0x12a4f):

1. Leading non-letters are spoken by name, one at a time, except `-`. An opening bracket stops this step when "bracketed text quieter" is on, so that the bracket reaches the word stage.
2. The rest is looked up in the voice's **exception list** (0x12f79): 149 entries of the form key → replacement, for abbreviations (`esim` → `esimerkiksi`, `klo` → `kello`, `mk` → `markkaa`), units, weekdays, names (`timehouse` → `taim#haus`), the name of every letter (`w` → `kaksoisvee`) and of every special character (`%` → `prosentti`). A final `.` on the word is tolerated and kept.
3. If the word is "puhelin", the digit-by-digit mode for the next number is switched on.
4. A word with no vowel in it is an abbreviation: each character is looked up and spoken by name.
5. Otherwise letters the voice has no sound for are rewritten: `w`→`v`, `q`→`k`, `æ`→`ä`, `ü`→`y`, `å`→`oo`, `z`→`ts`, `x`→`ks`, and `c`→`s` or `k` according to a setting.
6. The word goes to the word stage. Whatever trailing characters the word stage hands back (anything it has no rule for) are spoken by name.

## Stage 3b: numbers

`speak_number` (0x12c74) gets a run that began with a digit.

- **Digit by digit**, with a pause of 4000 for every non-digit, if the run starts with `0` followed by another digit, or if the previous word was "puhelin".
- Otherwise each group of digits (with one decimal separator) is converted to words by `number_to_words` (0x13175) and spoken as one word. The characters around the group choose one of seven inflected forms:

| Form | Chosen when | Example entries |
|---|---|---|
| 1 | default | yksi, kaksi, kymmenen, sata, tuhat |
| 2 | followed by `.` (or by `§`) | ensimmäinen, toinen, kolmas |
| 3 | followed by `-` | yhdestä, kahdesta |
| 4 | preceded by `-` | yhteen, kahteen |
| 5 | followed by `.` and preceded by `.` | ensimmäistä, toista |
| 6 | followed by `.-` | ensimmäisestä, toisesta |
| 7 | followed by `.` and preceded by `-` | ensimmäiseen, toiseen |

- Between groups: `,-` says "markkaa"; `$` "dollaria", `%` "prosenttia", `£` "puntaa", `°` "astetta"; other characters are looked up and spoken by name; `.`, `-` and the decimal separator are silent.

The words come from one list of 195 comma-separated entries in the program (data segment 0x0d1b). Numbers are built three digits at a time from the left with the group words biljoona, miljardi, miljoona, tuhat (0x132fe). A decimal part is read digit by digit after " pilkku ". Sixteen digits or more are spoken as "enemmän kuin paljon".

Two quirks of the original: a group equal to 1 gives the group word alone ("tuhat"), and when the *last* group is exactly 1 nothing is added, so `1001` is spoken "tuhat".

## Stage 4: one word becomes sounds

`speak_phonemes` (0x13543) receives one word in spelling with its punctuation, for example `päivää,`.

### Markers

Besides letters, the word stage understands four characters, which the exception list uses to tune pronunciations:

| Character | Effect |
|---|---|
| `_` before the first letter | the word starts unstressed |
| `#` | the next syllable is stressed |
| `-` | the next syllable is unstressed |
| `&` | a short pause (2000) |

So `timehouse` → `taim#haus` puts a stress on "haus", and `ky` → `koo&&yy` separates the two letter names.

### Punctuation and pauses

Non-letters after the last letter are taken off first. They set the pitch level and the pause after the word:

| Character | Pause after the word | Other effect |
|---|---|---|
| `,` | 8000 | |
| `;` | 10000 | |
| `:` | 12000 | |
| `!` | 13000 | every syllable of the word but the last is stressed |
| `?` | 14000 | the word starts unstressed |
| `.` | 16000 | sentence pitch level goes down for this word and is reset after it |
| `)` | | ends "quieter" if a bracket was open |

The pauses for `. : ? !` are skipped unless settings word 13 is set, and `PUHU.EXE` clears it whenever its ring buffer is 32,000 bytes or more, which it normally is. So in practice only `,` and `;` pause, and sentences follow each other without a gap in the stream.

Other pauses: 1500 before a word that begins with a vowel (not for the first word of a speak call), 7000 before an opening bracket.

A pause is counted in timer units, not samples; see [original-program.md](original-program.md#pauses).

### Stress level

Every syllable is spoken at one of three levels:

| Level | Meaning | Effect |
|---|---|---|
| 0 | stressed | longest durations, highest contour, volume plus 25% |
| 1 | normal | |
| 2 | word-final | shortest durations, lowest contour, one volume step quieter |

A word starts at level 0, or at 1 if it is marked unstressed, ends in `?`, or is shorter than five letters while first-syllable stress is switched off. After a vowel run at level 0 the level becomes 1 (it goes back to 0 if the word ends in `!`). The last vowel run of a word is spoken one level further down: a stressed monosyllable at 1, the last syllable of a longer word at 2. If the word ends in `!` or `?` the last run is at level 1.

### The sounds

The word is walked left to right. A doubled consonant is handled as one long consonant.

| Letters | How they are made | Source in the voice file |
|---|---|---|
| vowels `a e i o u y ä ö` | a stored pitch period, repeated | table 3, units 1–8 |
| `j l m n v`, and `ng` | a stored pitch period, repeated | table 3, units 9–14 |
| `b d f g k p t` | silence (the closure), then a stored burst chosen by the vowel that follows | table 1, units 1–7, up to 9 variants |
| `h r s` | a stored noise, repeated | table 2, units 1–3 |
| `c q w x z` | nothing: they were rewritten in stage 3 | |

**A vowel run** (0x1373f–0x13c20) is one or more vowels in a row. Each vowel gets a number of periods from the voice parameters, by stress level:

- a single vowel: `vowel periods`;
- the same vowel twice (a long vowel): the same again plus `long vowel extra periods`;
- a different vowel (a diphthong): `diphthong glide periods` of cross-fade from the previous vowel, then `diphthong second vowel periods`.

A **pitch contour** is laid over the whole run: the voice file holds a 256-step curve per stress level, and it is resampled to the number of periods in the run, one pitch byte per period (0x13905–0x13977).

**`j l m n v`** (0x13f76–0x141d5) are made the same way with a few periods of their own unit. Next to a vowel there is a cross-fade of `consonant-vowel glide periods` on each side. Their pitch is a straight ramp: it continues from the vowel before, or starts at a fixed value. How many periods depends on the position: between vowels, before a stop, at the word end, or otherwise; a doubled one gets extra.

**Stops** (0x13d0b–0x13ec7): the closure is a pause, sized so that pause plus burst fill a fixed slot (2200 samples in all three voices), plus 3000 for a doubled stop, scaled by speed. Then the burst is played once. `k p t d` have nine recorded bursts each, one for each following vowel and one for "no vowel follows".

**`h r s`** (0x13eca–0x13f73): the noise unit is played a number of times given by the voice parameters, more for a doubled consonant, scaled by speed.

### Durations and speed

All period counts go through one of two formulas (0x14560 and 0x1451e), with `x` the voice parameter:

```
dur6(x) = (pitch − 4)·(pitch_var + 4)·x / 128  +  (10 − speed)·x / 6
dur8(x) = (pitch − 4)·(pitch_var + 4)·x / 128  +  (12 − speed)·x / 8        (at least 1)
```

At speed 4 the second term is `x` itself; at speed 0 it is 1.67× or 1.5×, at speed 8 a third or a half. The first term adds periods when the pitch is raised, because each period is then shorter.

## Stage 5: periods become samples

### Cross-fade

`synth_voiced` (0x14f82) gets two periods, A and B, and a list of pitch bytes, one per output period. For every output sample it computes

```
out = (wA·A[i] + wB·B[i]) / 32        with wA + wB = 32
```

on samples centred at zero. The weights start at 31:1 and move one step towards 1:31 at regular intervals, so that the fade ends when the last period does. The same A and B are read again for every output period. For a steady sound, `synth_plain` (0x143f2) simply copies one stored period per output period.

### Pitch

Each output period has a pitch byte `c` (from the contour). `pitch_offset` (0x14dc9) turns it into a number of samples:

```
offset = ( c  +  level_offset  +  (pitch − 4)·(period / 16) ) · (pitch_var + 4) / 8
         clamped to ± period / 2
```

`level_offset` is the voice's pitch offset for the current sentence level (+5, 0, −15 in all three voices): level 1 for the first word of a sentence, 2 after it, 3 for the word with the full stop. A positive offset **cuts** that many samples off the end of the period, which raises the pitch; a negative one **appends** that many samples of silence, which lowers it. Nothing is resampled. So the pitch setting shifts every period, and pitch variation scales the whole deviation from the recorded pitch (0 halves it, 8 gives one and a half times).

The recorded pitch is the period length: 225 samples for Janne (98 Hz), 240 for Kristian (92 Hz), 105 for Sophia (210 Hz).

### Volume and emphasis

Volume is a bit shift by `volume − 4` (0x15114): each step doubles or halves the signal, and shifting up clips at the 8-bit limits. At the volume the voices are saved with, 8, the signal is multiplied by 16 and about 40% of the samples clip. That was deliberate: the output device was a PC speaker.

A stressed syllable is additionally multiplied by 1.25 (0x14e81); a word-final one is played one volume step lower. With consonant emphasis on, stops and noises are played two steps (4×) louder. Text in brackets is played one step lower when that setting is on.

## What comes out

A stream of two kinds of items, in order:

- runs of 8-bit unsigned samples (128 is silence; the value 0 never occurs, it is replaced by 1);
- pauses, as a count.

In the original these go into a ring buffer and are played by a timer interrupt through the PC speaker. In the port they go to a sink: either the samples as they are (`--device dac`), or a model of the PC speaker's pulse train (`--device pcspeaker`). See [original-program.md](original-program.md#output-stage-the-pc-speaker) and [pc-speaker.md](pc-speaker.md).

## Two bugs in the original word stage

Both are reproduced by the port, because fixing them changes the output and the byte-for-byte comparison with the original would be lost for the affected words.

1. **Word-initial `j l m n v`** (0x14146). The pitch bytes for the consonant are written at index `count` *before* the count is incremented. When the list is empty the first byte overwrites the count itself, the following ones land at the end of the array, and the bytes that are then used were never written: they are whatever the stack held, usually the pitch contour of the previous word.
2. **Diphthong glide** (0x13b2d). The glide is told it has one more pitch byte than was copied for it, so its last period uses a stale byte from the same array.

The effect is a slightly wrong pitch on a few periods. After a number the stale bytes can be leftovers of pointers and return addresses, which is the one place where the port's output can differ from the original's by a few samples.
