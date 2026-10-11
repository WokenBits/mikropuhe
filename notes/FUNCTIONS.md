# MikroPuhe 1.0 — function map

Path from `main` to the speaker. Addresses are linear (image at segment 0x1000); each has a listing in `evidence/functions/<address>.txt`. Names are mine.

"Read" says how far the claim goes: **asm** = read from the instruction list, **dec** = read from Ghidra's pseudocode only (control flow is usually right, references to globals often are not), **skim** = purpose taken from callers, callees and a first look, **no** = not read.

Globals are offsets in the data segment (DS = 0x194a, linear 0x194a0 + offset).

## Port status (2026-10-09)

| Original | C function | File | Covered by |
|---|---|---|---|
| 0x14813 | `mp_voice_load` | `port/src/voice.c` | both checks (every case loads a voice) |
| 0x13543 and its helpers 0x142de, 0x1433a, 0x14384, 0x143f2, 0x14503, 0x1451e, 0x14560, 0x145a1 | `mp_speak_phonemes`, `cons_unit`, `voiced_unit`, `plain_unit`, `synth_plain`, `pause_scale`, `dur8`, `dur6`, `scale` | `port/src/synth.c` | `make check-phonemes`: 68 of 68 cases byte-identical per word |
| 0x14d5c, 0x14d91, 0x14dc9, 0x14e1a, 0x14e81, 0x14f46, 0x14f82, 0x15114 | `is_vowel`, `is_consonant`, `pitch_offset`, `find_unit`, `emphasise`, `copy_quieter`, `synth_voiced`, `apply_volume` | `port/src/synth.c` | same |
| 0x1548a, 0x1551d, 0x1589a (+0x15882) | `mp_dev_start`, `mp_dev_pause`, `dev_write` | `port/src/synth.c` | same |
| 0x14605, 0x145e8 | `mp_set`, `clamp_setting` | `port/src/synth.c` | `make check-e2e` |
| 0x152b7, 0x15291 | `mp_pcspk_table`, `mp_pcspk_too_slow` | `port/src/pcspk.c` | `make check-pcspk`: table identical for 12 calibrations |
| 0x15616 (pulse pattern), 0x156df (pause length) | `mp_pcspk_samples`, `mp_pcspk_pause` | `port/src/pcspk.c` | `make check-pcspk`: passes per sample identical; the timing around them is a model |
| 0x126eb, 0x12651, 0x1262e, 0x12a4f, 0x129ee, 0x12f79 | `mp_speak`, `char_class`, `is_letter`, `speak_word`, `replace_char`, `lookup_exception` | `port/src/text.c` | `make check-e2e`: stage calls identical in 89 of 89 cases |
| 0x12c74, 0x13175, 0x132fe, 0x130c5, 0x130f9, 0x14db1 | `speak_number`, `number_to_words`, `num_groups`, `num_append`, `num_word`, `digit_value` | `port/src/text.c` | same |
| 0x10669, 0x108ef, 0x1099c, 0x10ce0, 0x12398 (voice switch only) | `parse_option`, `speak_run`, `speak_piece`, `speak_text`, `load_voice` | `port/src/puhu.c` | same |

| 0x11c57, 0x11bd5, 0x1121b, 0x111cf | `run`, `title`, `key_bar`, `quit` | `port/src/ui.c` | `make check-ui`: screen, colours, cursor and speech identical for 126 key sequences |
| 0x11a26, 0x11713, 0x112c6, 0x11684, 0x1203a | `settings`, `settings_row`, `draw_row`, `voice_changed`, `error_box` | `port/src/ui.c` | same |
| 0x15f91, 0x15e71, 0x159d8, 0x15ac6, 0x15bd5, 0x15c85, 0x1606a, 0x15d60, 0x15981, 0x1594c | `field_init`, `field_draw`, `field_redraw`, `field_redraw_from`, `field_left`, `field_right`, `field_key`, `frame`, `put_at`, `field_cursor` | `port/src/ui.c` | same |
| 0x11563 | `ui_save` | `port/src/puhu.c` | tried on a writable copy of a voice |

Not ported, by design: the calibration against the timer chip (0x15167, replaced by machine presets), the ring buffer and timer interrupt, the resident driver, the keyboard hook (and with it, stopping speech by a key), the interface's monochrome colours, the screen echo of spoken text on the command-line path.

The tables below were written while reading and are kept as the map; where a row says "no" or "skim" and the function is in the list above, the C source is now the better description.

## Program (segment 1065, 111c, 11ec)

| Address | Name | Read | What it does |
|---|---|---|---|
| 0x11047 | `main` | dec | Banner; no arguments → `ui_main`; `?` → usage; options; `:FILE` → `speak_file`, else `join_args` + `speak_text` |
| 0x10669 | `parse_option` | asm | One `/XXXn` option at a text position; returns its length or 0. Jump table at 0x106e4 |
| 0x1082b | `parse_options` | dec | Repeats `parse_option` over one argument |
| 0x10e29 | `join_args` | dec | Joins the remaining arguments with spaces into a heap string (0x1a30) |
| 0x10efa | `speak_file` | dec | Opens the file, reads 16,000-byte pieces, cuts at 0x1A, calls `speak_text` |
| 0x10ce0 | `speak_text` | dec | Cuts at `. : ? !` + space/newline, speaks each piece with `speak_piece`; handles Esc, space, F2 between pieces |
| 0x1099c | `speak_piece` | dec | Echoes the text line-wrapped when 0x12b is set; finds inline options (0x108a0); passes runs of text to 0x108ef |
| 0x108ef | `speak_run` | skim | Calls `speak_checked` |
| 0x11fbf | `speak_checked` | dec | `lib_speak(text, buffer 0x1a92, size 0x1ac6)`; on failure reports the library error |
| 0x12398 | `select_voice` | skim | Closes the current voice, loads another (0x14a86 / 0x14b66), prints the load errors |
| 0x11c57 | `ui_main` | asm | Full-screen interface: input field, history of ten lines, F1 help, F2 settings, Esc. See `docs/interface.md` |
| 0x124ea | `serial_number` | asm | The inverted 32-bit value after the string at DS:0x096d |

## Speech library, C part (segment 1262)

| Address | Name | Read | What it does |
|---|---|---|---|
| 0x14a86, 0x14b66 | `lib_open` (two variants) | skim | Driver search (0x14cb8), `load_voice`, device init (0x1544a) |
| 0x14b29 | `lib_close` | skim | Frees the voice |
| 0x14813 | `load_voice` | dec | Reads the `.SMP` container: signature, name, 46-byte settings, three lengths, three blocks into one allocation (pointers 0x1b09, 0x1b50, 0x1b0d) |
| 0x14794 | `voice_read` | dec | Reads from the open file, or copies from memory when handle 0x1b55 is −1 |
| 0x14605 | `set_settings` | dec | Copies 14 words into the active settings at 0x1b19: four scaled values through 0x145e8, ten flags (see below) |
| 0x145e8 | `clamp_setting` | asm | A value outside 0..8 becomes 4 (and sets an error flag at 0x1b11) |
| 0x14761 | `get_settings` | dec | Copies the active settings out |
| 0x126eb | `lib_speak` | asm | Entry point. Splits the text into runs by character class and dispatches (see below). Sets up the ring buffer (0x1548a), hooks the keyboard if 0x1b2d, ends with 0x154e1 |
| 0x12651 | `char_class` | dec | Lower-cases A–Z and `ÅÄÖÜ` (table 0xced), then: 1 = letter (in `abcdefghijklmnopqrstuvwxyzåäöü_#`, 0xccc), 2 = digit (ctype table 0x167d), 3 = one of the characters at 0x151b, 4 = anything else |
| 0x1262e | `is_letter` | dec | `memchr` in the 32-byte alphabet at 0xccc |
| 0x12a4f | `speak_word` | dec | Leading non-letters one by one; exception lookup for the whole word; the word "puhelin" sets 0x1b08; a word without vowels (0x14d5c) is spelled letter by letter; otherwise letter substitutions (`wqæüåzx` table at 0xcf7 → strings at 0xcff, `c` → 0x1b12, optionally `j`) and `speak_phonemes` |
| 0x129ee | `replace_char` | dec | Replaces one character in place by a string |
| 0x12f79 | `lookup_exception` | dec | Searches voice block 1: count word, then records `len, key, len, replacement`; a trailing `.` on the key is optional. Rewrites the string in place, returns 1 on a hit |
| 0x12c74 | `speak_number` | dec | After "puhelin" or with a leading 0 followed by a digit: digit by digit, a non-digit giving a pause of 4000. Otherwise digit groups through 0x13175, and four special characters through a switch (table at 1262:08a8, cases not read). The unit words at 0x1569–0x158d (markkaa, prosenttia, astetta, dollaria, puntaa) are probably spoken there |
| 0x13175, 0x132fe | `number_to_words`, `num_groups` | dec | Digit string → words from the comma-separated list at 0xd1b: 195 entries in seven inflected forms (form bases at 0x14cc, group sizes at 0x14da). Words are joined without spaces |
| 0x130c5, 0x130f9 | `num_append`, `num_word` | dec | Append a string / entry n of a form to the output buffer |
| 0x13543 | `speak_phonemes` | asm | The core: one word in ordinary spelling → unit selection, durations, pitch → `synth_voiced` / `synth_plain` / `plain_unit`. About 3.4 KB, 0x35E bytes of locals; Ghidra cannot decompile it. Full listing in `evidence/disasm-13543.txt` |
| 0x143f2 | `synth_plain` | dec | Copies whole periods from a unit, applies volume, optional ×1.25, writes them; per-period length correction from `pitch_offset` |
| 0x14384 | `plain_unit` | asm | Optional closure pause (0x14503), then a stored unit written n times with volume, two steps louder when consonant emphasis is on. Called from the jump-table cases of 0x13543 that Ghidra did not follow |
| 0x1451e, 0x14560 | `dur8`, `dur6` | asm | A duration in pitch periods from a voice parameter, scaled by speed (÷8 or ÷6) and by pitch × pitch variation |
| 0x142de, 0x1433a | `cons_unit`, `voiced_unit` | asm | Consonant → unit number: b d f g k p t → 1..7 of table 1; j l m n v → 9..13 of table 3 |
| 0x14503, 0x145a1 | `pause_scale`, `scale` | asm | Closure pause and noise repeat count, scaled by speed |

Character-class dispatch in `lib_speak` (asm, 0x1283c–0x1297a). Characters are collected into a run (at most 100; a longer run is forced to end). With the class of the previous character and of the new one:

| Previous | New character | Action |
|---|---|---|
| 1 (letter) | class 3 | stays in the run, treated as class 1 |
| 1 | class 2 or 4 | run → `speak_word`, run restarts |
| 2 (digit) | space, or class 3 | stays in the run, treated as class 2 (the space itself is not stored) |
| 2 | class 1 or 4 | run → `speak_number`, run restarts, 0x1b08 cleared |
| 3 (special) | class 1 or 2 | no flush: the run continues and takes the new class |
| 3 | class 4 | each character of the run → `lookup_exception`, and if it hits, `speak_phonemes` |
| 4 | any | nothing to flush |

A class 4 character is not stored. A newline gives one pause of 20,000 (not repeated for consecutive newlines).

Settings block (14 words at 0x1b19, same order as in the `.SMP` file):

| Word | Global | Meaning | Source |
|---|---|---|---|
| 0 | 0x1b19 | speed (NOP) | option parser, `set_settings` |
| 1 | 0x1b1b | pitch (KOR); `pitch_offset` uses value − 4 | 0x14dc9 |
| 2 | 0x1b1d | pitch variation (KRV); `pitch_offset` uses value + 4 | 0x14dc9 |
| 3 | 0x1b1f | volume (VOI); shift = value − 4 | 0x15114 |
| 4 | 0x1b21 | consonant emphasis (KNP) | option parser |
| 5 | 0x1b23 | first-syllable stress (ETP) | option parser, 0x143f2 |
| 6 | 0x1b25 | speak a lone underscore | 0x12f79 |
| 7 | 0x1b27 | decimal separator: non-zero `.`, zero `,` (kept in 0x1b54) | 0x126eb |
| 8 | 0x1b29 | bracketed text quieter (0x1b47) | 0x126eb, 0x12c74 |
| 9 | 0x1b2b | letter C spoken as `k` (non-zero) or `s` (kept in 0x1b12) | 0x14605 |
| 10 | 0x1b2d | hook the keyboard interrupt while speaking | 0x126eb |
| 11–12 | 0x1b2f–0x1b31 | not identified | |
| 13 | 0x1b33 | sentence-end punctuation gives a pause in the word stage. Set by the program, not by the user: 1 only when the ring buffer is smaller than 32000 | 0x14297, 0x10ce0 |

Words 6–9 match four of the interface labels by behaviour ("Puhutaanko alaviiva", "Suluissa oleva teksti puhutaan hiljempaa", "C:n korvaus"); word 7 as the decimal separator is my reading, the label list has no obvious match.

## Speech library, assembly part (0x14d50–0x15167)

| Address | Name | Read | What it does |
|---|---|---|---|
| 0x14d5c | `is_vowel` | asm | Position of the character in an 8-byte list at 14d5:0004, 0 if absent |
| 0x14d91 | `is_consonant` | asm | Same with a 21-byte list at 14d5:002c |
| 0x14db1 | `digit_value` | asm | `'0'..'9'` → 1..10, else 0 |
| 0x14dc9 | `pitch_offset` | asm | Signed length correction for one period from a signed byte, the level table at block 0 + 0x2E (index 0x1b13), pitch and pitch variation; clamped to ± period/2 |
| 0x14e1a, 0x14e5e, 0x14e70 | `find_unit` | asm | Record lookup in block 2 through its 4-byte offset table |
| 0x14e81 | `emphasise` | asm | ×1.25 with clamping, in place |
| 0x14f82 | `synth_voiced` | asm | Cross-fade between two units, period by period (weights 31:1 … 1:31), length correction, volume, optional emphasis, write |
| 0x15114 | `apply_volume` | asm | Shift by volume − 4 with clamping |
| 0x14ed5, 0x14f13, 0x14f32 | keyboard hook | asm | int 9 handler and its install/remove |

## Device layer (0x15167–0x1594c) and driver interface (0x14ca0–0x14d3b)

| Address | Name | Read | What it does |
|---|---|---|---|
| 0x1544a | `dev_init` | asm | `calibrate` + `build_pwm_table`; returns 64000 through its argument |
| 0x15167 | `calibrate` | asm | Times two delay loops against the PIT; "too slow" if a pulse cannot fit in 45,454 ns |
| 0x152b7 | `build_pwm_table` | asm | 257 delay counts at 0x1ddc |
| 0x1548a | `dev_start` | asm | Sets the ring buffer, installs the timer handler (0x15780) |
| 0x154e1 | `dev_stop` | asm | Drains, restores int 8 and the BIOS clock (0x157b4) |
| 0x1551d | `dev_pause` | asm | Pause of n: literal 0x80 samples below 110, else the record `00 lo hi` |
| 0x155e5 | `dev_pending` | asm | Bytes waiting in the ring buffer |
| 0x1589a | `dev_write` | asm | Copies samples into the ring buffer, optional volume, zero bytes → 1 |
| 0x15616 | `player` | asm | Pulse-width playback until the buffer is empty or a pause record |
| 0x156df | `arm_pause` | asm | PIT one-shot for a pause |
| 0x15741 | `timer_isr` | asm | int 8 while speaking |
| 0x14cb8 | `find_driver` | asm | Signature search over int 0x80–0xFF |
| 0x14ce9 | `driver_call` | asm | Forwards a library call to the resident driver |

The port boundary: everything in the device layer is replaced by an output sink that receives `dev_write` runs and `dev_pause` counts. `tools/puhu_emu.py` cuts at the same place, one level lower (the ring buffer).
