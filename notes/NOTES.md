# MikroPuhe 1.0 — running notes

## 2026-10-09 — setup and triage

Machine: ai (Arch Linux), REA at upstream commit `8c1bec4` (6.2.0 checkpoint), Ghidra 12.1.4, JDK 21.0.12, Node 24.18.0.

Ran, from `mikropuhe-1.0/` on ai, with `R=~/projects/rea/.claude/bin/rea-dev` and `T=$PWD/original/files/PUHU.EXE`:

| Command | Output | Result |
|---|---|---|
| `cd original && sha256sum -c SHA256SUMS` | — | 5 of 5 OK |
| `$R inspect "$T" --provider ghidra --json` | `evidence/inspect.json` | exit 0, 21 s; format `dos-mz`, 93 segments |
| `$R search "$T" --pattern='.' --mode regex --kind procedures --provider ghidra --json` | `evidence/procedures.json` | exit 0; 262 procedures |
| `$R search "$T" --pattern='.' --mode regex --kind strings --provider ghidra --json` | `evidence/strings.json` | exit 0; 30 strings |
| `$R function "$T" entry --provider ghidra --json` | `evidence/function-entry.json` | exit 0; 3.6 MB (not read yet) |
| `objdump -D -b binary -m i8086 --adjust-vma=0x10000` on the image without its 3,072-byte header | `evidence/puhu.objdump.txt`, port lines in `evidence/port-io.txt` | linear sweep, so data is disassembled as code in places |

Findings are in `../README.md` under "What is known".

Notes on the tools:

- Ghidra found only 30 strings; `strings -n 5` on the file shows many more (the Finnish UI text). The gap is probably DOS code page bytes breaking Ghidra's ASCII string detection. Do not treat the Ghidra string list as complete.
- `function entry` returned 3.6 MB for the C startup routine. Ask for specific functions and filter output (`--filter-output`) rather than reading whole envelopes.
- The objdump listing is a cross-check for finding instructions (port I/O, interrupts), not a source of function boundaries. Use Ghidra for those.

Next: step 1 and step 2 of `../port/PLAN.md`. The first function to read is the one containing 0x15193 (timer reprogramming), then its callers back toward `main`.

## 2026-10-09 (second session) — output stage, harness, reference set

Machine and versions as above. New Python venv `~/tools/mikropuhe-venv` (unicorn 2.1.4, capstone 5.0.7, numpy); `P=~/tools/mikropuhe-venv/bin/python`.

### What was run

| Command | Output | Result |
|---|---|---|
| `$R function "$T" <addr> --provider ghidra --json` for all 262 addresses in `evidence/procedures.json`, 8–18 in parallel | envelopes in a scratch directory (about 300 KB each, not kept) | 259 exit 0; 0x10669, 0x112c6, 0x13543 fail every time: `Ghidra decompilation failed … AddressOutOfBoundsException: Offset must be between 0x0 and 0x10ffef, got 0x194a1aa0` |
| `$R instructions "$T" <addr> --provider ghidra --json` for those three | scratch | exit 0 (0x13543: 755 instructions) |
| `python3 tools/fn_extract.py <envelopes> evidence/functions <instruction envelopes>` | `evidence/functions/0x*.txt`, `INDEX.tsv` | 262 files, 1.7 MB: body, callers, callees, assembly, pseudocode |
| `$P tools/mzdis.py original/files/PUHU.EXE <start> <end>` | read on screen | relocation-aware capstone listing; used where Ghidra stops at a jump table |
| `$P tools/puhu_emu.py --out X -- "hyvää päivää"` | scratch | exit code 0 in 0.15 s; 29,959 samples + one pause of 3,135; two runs byte-identical |
| `$P tools/make_reference.py` (twice) | `evidence/reference/MANIFEST.tsv` | 68 cases, 0 failed, 532.5 s of audio; manifest SHA-256 `f829d6f4…` both times |
| `port/puhu-emu -o a.wav …`, `-o b.mp3`, `-o -`, `--nominal-pauses` | scratch | WAV 8-bit 22000 Hz, MP3 (lame), WAV on stdout all produced; with `--nominal-pauses` the WAV payload hash equals the manifest entry `jan-s01-default` |

Not run: live playback (no one listening on ai), anything in DOSBox or QEMU (not needed, see below).

### Observed

Output stage (PC speaker):

- 0x15167 calibrates CPU speed: installs an int 8 handler (14d3:000c), sets PIT channel 0 to divisor 0x0953 (about 500 Hz), times a 65,000-iteration `loop` (0x1583e) and a 6,500-pass dummy of the player loop (0x15844) over 25 ticks each, stores nanoseconds per iteration in DS:0x1dd4 and DS:0x1dd8, restores the PIT. Returns 0xFFFF when the two times sum to 45,454 ns (0xB18E) or more. Its caller 0x1544a then sets DS:0x7f = 100 and fails.
- 0x152b7 builds a table of 257 delay counts at DS:0x1ddc from the constant 22,727 ns (0x58C7): for s = 0..128, `22727 ± s*22727/(64 + s/2)`, converted to loop iterations with the calibration values.
- 0x15616 is the player. Per ring-buffer byte b ≠ 0: speaker bit on (`out 61h`), wait table[b+1] iterations, bit off, wait table[0] − table[b+1]. So one byte = one pulse of fixed period, width set by the byte. A byte 0 is followed by a 16-bit count: a pause.
- 0x156df times a pause with PIT channel 0 in mode 0: count = min(n, 0x3FF) × 64 ticks, remainder in DS:0x15cc for the next interrupt; n below 22 is raised to 22.
- 0x15741 (1561:0131) is the int 8 handler during speech: it calls the player, then re-arms the PIT for a pause.
- 0x157b4 (end of speech) restores int 8 and adds DS:0x1fe2 (32-bit count of bytes and pause units played) / 1208 to the BIOS tick counter at 0040:006C.
- 0x1589a copies samples into the ring buffer (far pointer DS:0x1dd0, size DS:0x1fe0, write index DS:0x1dce, read index DS:0x1fde), optionally applies volume (0x15114), and 0x15882 replaces every 0 byte in the copied run with 1.
- 0x1551d writes a pause: below 110 it writes that many 0x80 bytes as samples, otherwise the record `00 lo hi`.
- 0x1544a returns 64000 through its pointer argument on success.
- Each of these entry points first tests a flag byte (DS:0x1da3–0x1da9) and, if set, forwards to 0x14ce9 with a function number 1–7. 0x14cb8 looks for a resident driver: it walks interrupt vectors 0x80–0xFF and compares 24 bytes at handler+2 with the 24 bytes at 0x14ca0 (file offset 0x58A0): the text `MikroPuhe-ohjainTIME` followed by AB B6 B2 BA.
- The byte pair `CD BB` that objdump printed as `int $0xbb` is at 0x1aa82, inside the data segment.
- Ports seen in the harness for a spoken sentence: 0x3DA read (121×), 0x43 and 0x40 written (2× and 4×). Port 0x61 is not touched because the player is replaced.

Sample generation (assembly module 0x14d50–0x15167):

- 0x14f82 produces the samples: it mixes two source waveforms with weights that step from 31:1 towards 1:31 (sum 32, shift right 5 with rounding), one pitch period at a time into a buffer at DS:0x1b5b, pads or shortens each period by an offset from 0x14dc9, then calls 0x1589a. Period length is the word at DS:0x1d9f.
- 0x15114 is volume: `shift = [DS:0x1b1f] − 4`; positive shifts left with clamping to −128..127, negative shifts right.
- 0x14e81 multiplies samples by 1.25 with clamping.
- 0x14e1a finds a record in voice block 2 through a table of 4-byte offsets at its start.

Program structure:

- `main` is 0x11047: no arguments → full-screen interface (0x11c57); a single `?` → usage; otherwise options, then either `:FILENAME` (0x10efa reads the file in 16,000-byte pieces and stops at byte 0x1A) or the remaining arguments joined with spaces (0x10e29). 0x10ce0 cuts the text at `. : ? !` followed by a space or newline and speaks piece by piece through 0x1099c → 0x108ef → 0x11fbf → 0x126eb.
- Option parser 0x10669 (jump table at 0x106e4, ten entries): an option starts with `/`, `+` or `-`, then three letters compared case-insensitively against `NOPKORKRVVOIKNPETPJANKRISOPEIT`. The first six take one digit and store it as a word at DS:0x1a98, 0x1a9a, 0x1a9c, 0x1a9e, 0x1aa0, 0x1aa2. JAN, KRI, SOP set the voice index DS:0x1a96 to 0, 1, 2. EIT clears the byte DS:0x12b. Options chain without spaces (`/KRI/NOP8`). 0x108a0 calls the same parser on the text being spoken.
- In the harness, digit 9 gives the same stream as digit 4 for NOP, KOR and VOI (manifest rows `jan-s03-nop9` = `nop4`, `kor9` = `kor4`, `voi9` = `voi4`). Speed 0 is slowest and 8 fastest (172,093 / 116,087 / 46,165 samples for s03).
- The banner and the echoed text go to video memory, not to DOS handle 1 (stdout capture is empty, the text is in the harness's screen dump).

Voice files (loader 0x14813, checked by arithmetic on all three files):

| Offset | Size | Content |
|---|---|---|
| 0 | 51 | `MikroPuhe - näytetiedosto\r\nTimehouse Oy 1990\r\n`, 0x1A, `TIME` (compared with DS:0x14e2) |
| 51 | 1 | name length L (at most 32 bytes are kept, in DS:0x1d5b) |
| 52 | L | voice name |
| 52+L | 46 | settings block, loaded to DS:0x1b19: words speed, pitch, pitch variation, volume, consonant emphasis, first-syllable stress, 8 more words, then 18 bytes 0xFF |
| 98+L | 12 | three 32-bit lengths: 3000, 1825, and 34,544 (Janne) / 34,596 (Kristian) / 36,624 (Sophia) |
| 110+L | 3000 | block 0, pointer kept at DS:0x1b09 |
| | 1826 | block 1 (1825 rounded up to even), pointer at DS:0x1b50; begins `95 00 00 00` and holds short text pairs such as `ns` / `niin&sanottu`, `oy` / `_oo&yy` |
| | third length | block 2, pointer at DS:0x1b0d; begins with 32-bit offsets 16, 24430, 29500, 0 (Janne) |

Sizes add up exactly for Kristian and Sophia. JANNE.SMP has four more bytes at the end: `0D 0A 0D 0A`. Blocks 0 and 1 start with the same bytes in all three files.

Saved settings in the files as received: Janne 0,4,0,8,1,1; Kristian 3,5,8,8,0,1; Sophia 0,0,0,8,1,1 (speed, pitch, pitch variation, volume, consonants, stress).

At volume 8 the captured stream for "hyvää päivää" has 43% of its samples at ±127; at volume 4, 0.1%.

### Inferred

- Sample rate is 22,000 Hz nominal: the pulse period is 2 × 22,727 ns = 45,454 ns, and the clock correction divides by 1208 per 18.2 Hz tick (1208 × 18.2065 = 21,993). The real rate on a PC depended on the calibration.
- A pause unit on the PC lasted 64 PIT ticks = 53.6 µs, not one sample period (45.45 µs), so pauses were 1.18 × their count in samples. `port/puhu-emu` renders them that way by default; the manifest hashes use the plain count.
- The pulse-width table makes the speaker stage non-linear: duty = ½ ± ½·s/(64 + s/2), twice the gain for small signals. With the D/A card driver the samples presumably went out unchanged, but that driver is not in the zip.
- Valid setting digits are 0–8 and anything else becomes 4, probably in the validator 0x14605 (not read yet).
- `EIT` means "no text echo": the byte it clears gates the `printf` of the text in 0x1099c.
- The D/A card messages ("En löydä D/A-muunnin lisäkorttia") belong to error codes returned by the resident driver; PUHU.EXE itself contains no D/A card code. Not checked against 0x14ce9's callers.
- Volume 8 was the intended setting for the PC speaker (as loud as possible, close to a 1-bit signal); volume 4 is the undistorted signal.
- Block 1 is a table of abbreviations and spelling exceptions, block 2 the waveform units with an index, block 0 timing and pitch parameters. From their first bytes only.

### Unknown

- Everything between the text and 0x14f82: text rules, numbers, unit selection, durations, the pitch contour. The functions are 0x126eb, 0x12a4f, 0x12c74, 0x12f79, 0x13175, 0x132fe, 0x13543 (about 3.4 KB, the largest), 0x143f2.
- The internal layout of the three voice blocks.
- Whether Ghidra's 262 procedures are all of them. 0x13543–0x142de is 3,483 bytes but the function's instruction list has 755 entries; code reached only through jump tables may be missing.
- What the serial number shown in the banner is derived from (0x124ea). The harness shows `Sarjanumero ···`.
- How a real PC speaker and its drive circuit coloured the sound.

### Tool notes

- `rea function` returns no result at all when the decompiler throws, although the assembly, callers and callees are available (`rea instructions` works on the same function). All three failing functions contain a `JMP CS:[BX+table]` switch. Worth reporting upstream: it contradicts "preserve partial facts" in AGENTS.md. Not reported yet.
- `--snapshot` does not shorten a `function` call here (20–24 s with or without).
- Ghidra's pseudocode gets data references wrong in this program: a pushed `DS:0x14d` appears as `s_print_scanf… + 0xc`. Read the pushed constant from the assembly and look it up at DS = 0x194a (linear 0x194a0 + offset).
- Ghidra treats the initialised data segment as constant. Where a function tests a global that lives there, the decompiler folds the test and deletes the other branch (0x126eb decompiles to four lines with 60 "Removing unreachable block" warnings). For such functions only the assembly counts.
- Ghidra names 0x194a0–0x1aec8 `CODE_90`; it is the initialised data segment. Globals above 0x1aec8 are uninitialised data.
- Do not name a helper `dis.py`: it shadows a standard-library module that capstone imports.

### Harness design (tools/puhu_emu.py)

The original image is loaded at segment 0x1000 in Unicorn (16-bit real mode) with a minimal DOS and BIOS: int 21h functions the program actually calls, int 10h/11h/12h/16h/1Ah, port 0x3DA toggling. Three patches are applied to the in-memory copy only: 0x15167 returns 0, 0x152b7 returns immediately, 0x15616 returns 0 after a Python hook has emptied the ring buffer into the event list. No interrupt is ever delivered, so the keyboard handler and the timer handler never run. Everything upstream of the ring buffer is original code.

Limits: one sentence source per run; file writes are refused; the full-screen interface is not driven (no keys are queued).

Next: read 0x126eb and the functions under it, top down, and name them in `notes/FUNCTIONS.md`. Use the harness to dump intermediate data at function boundaries (text after expansion, the unit list) so each stage of the port can be checked on its own.

### Listening check (the owner, 2026-10-09)

The owner played `port/demo/janne-speed4-vol4.mp3`, `kristian-vol4.mp3` and `sophia-speed4-vol4.mp3` (made by `port/puhu-emu`, raw 8-bit stream, volume 4) and found them good. So the stream captured at the ring buffer is intelligible speech, and the harness is accepted as the reference. The volume 8 default and the `--pc-speaker` rendering were not commented on.

## 2026-10-09 (third session) — word stage ported and verified

- Read all of 0x13543 (`speak_phonemes`) from a capstone listing that includes the two jump-table regions Ghidra skipped: `evidence/disasm-13543.txt`. Transcribed to C in `port/src/synth.c` together with the assembly sample generator (0x14d5c–0x15114), the device boundary (0x1551d, 0x1589a) and the helpers 0x142de, 0x1433a, 0x14384, 0x143f2, 0x14503, 0x1451e, 0x14560, 0x145a1, 0x145e8. Loader in `port/src/voice.c`.
- `tools/puhu_emu.py` gained stage tracing (`--calls`, `Dos.enable_trace`): every `speak_phonemes` call with its argument, the globals it depends on, its return value and the stream position after it.
- **Check:** `make -C port check-phonemes` replays the traced calls of all 68 reference cases through the C code and compares the sample stream word by word. Result: 68 cases, 0 failed.

Observed while doing this:

- The word stage receives ordinary spelling, one word per call with its punctuation attached (`hyvää`, `päivää,`, `kaksitoista`, `vee_see.` for "wc."). Vowels are `aeiouyäö` (list at 0x14d54), consonants `bcdfghjklmnpqrstvwxyz` (0x14d7c).
- Sounds by class (jump table at 0x13ce9): `b f g k p t` and `d` are stored bursts from table 1 of block 2, chosen by the following vowel, preceded by a closure pause; `h r s` are noise units from table 2, repeated; `j l m n v` and all vowels are single pitch periods from table 3, repeated and cross-faded (0x14f82), with one pitch byte per period. `c q w x y z` produce nothing at this stage (they are rewritten earlier). `ng` is unit 14 of table 3.
- Block 0 of a voice: words at 0x00–0x3F are durations and pitch parameters (0x36 = samples per pitch period: 225 Janne, 240 Kristian, 105 Sophia; 0x30/0x32/0x34 = pitch offset per sentence level), and from 0x100 three 256-byte pitch contour templates, one per stress level.
- A table in block 2 is a run of units; a unit is a count followed by that many (length, data padded to even) variants (0x14e1a).
- Pause lengths written by the word stage: 1500 before a word-initial vowel, 7000 before an opening bracket, 2000 for `&`, and after punctuation `,` 8000, `;` 10000, `:` 12000, `!` 13000, `?` 14000, `.` 16000 (table at 0x1422d). The sentence-end ones are skipped when settings word 13 is 0.
- **A bug in the original, reproduced on purpose.** For a word-initial `j l m n v` the code at 0x14146 stores the consonant's pitch bytes at index `contour[0]` before incrementing it, so with an empty contour they land at the end of the array and the bytes that are read (0x1419b → 0x143f2) are uninitialised stack. Without those bytes the C code matched only 10 of 68 cases; with the harness's stack bytes fed in, 68 of 68. The stack position of the frame depends on the caller: bp = 0x1500 when called for a word, 0x13fa for a number word.
- The same loop has an off-by-one in the other direction at 0x13b2d (`tmp[0]` is one more than the number of pitch bytes copied).
- Settings word 13 (DS:0x1ab2 in the program's copy) is not a user setting: `speak_text` (0x10ce0) sets it to "ring buffer smaller than 32000" before speaking and back to 1 after. With the 64000-byte buffer it is 0 while speaking.

Inferred: stress level 0 is the stressed first syllable (volume plus ×1.25), 1 normal, 2 the quieter word-final syllable; the level also selects the duration column and the contour template.

Unknown: how far the stale stack bytes can be reproduced without the oracle. The port keeps its frame image between calls, which is right when two consecutive calls come through the same caller. To be measured once the text stage exists.

### Later the same session — text stage and the native `puhu`

Written: `port/src/text.c` (0x126eb `mp_speak` and everything it calls down to the word stage), `port/src/puhu.c` (program layer after 0x11047 / 0x10ce0 / 0x1099c, plus UTF-8 input and the outputs), `port/src/tables.h` (generated by `tools/gen_tables.py` from the program's data segment), `port/tests/check_e2e.py`. Build: `make -C port` gives `port/build/puhu`.

| Command | Result |
|---|---|
| `make -C port check-phonemes` | 68 cases, 0 failed |
| `make -C port check-e2e` (68 reference cases + 22 extra inputs) | 90 cases, 3 failed, 1 skipped. In all 89 that ran, the sequence of stage calls (every word handed to the word stage, with pitch level, bracket state and volume, and every pause) is identical to the original's. In 86 the sample stream is byte-identical too. |
| `tests/check_e2e.py --no-stack` (port starts from a zeroed stack image) | the same 2 of 68 fail: the stack bytes the original starts with do not matter for the reference set |
| 10 awkward inputs through `build/puhu-asan` (AddressSanitizer + UBSan), event and WAV output | 0 reports |
| `puhu` against `puhu-emu`, same sentence and options, default / `--pc-speaker` / `--nominal-pauses` | identical WAV files for a sentence without digits |
| `time build/puhu -o x.wav -f README.md` | 0.02 s |

Observed:

- The three failing cases (`jan-s07`, `kri-s07`, `extra-07`) differ by 10, 30 and 1 samples out of 340,000–470,000. In each the differing word follows a number or a special character that goes through the number handler. Comparing the two stack images word by word shows where: the bytes the port lacks are `tmp[0..12]` of the word-path frame, which in the original hold the return address, the far-pointer argument, two saved registers and a local of the number handler's last call, and `tmp[2..5]` of the number-path frame, which hold the saved string pointer (`[bp-0x208]`) of the last word-path call.
- These bytes reach the output through the second off-by-one (0x13b2d): a diphthong's glide is given one more pitch period than was copied, so one period uses a stale byte.
- Number expansion in the original: groups of three digits from the left with the group words from the list; a group equal to 1 gives the group word alone ("tuhat"); as a consequence `1001` is spoken "tuhat" and nothing more (0x132fe: the last group being exactly 1 emits nothing). Sixteen or more digits give the entry "enemmän kuin paljon". The inflected form is chosen from the neighbours: a following `.` gives the ordinal, `-` before or after gives case forms (0x12d5f–0x12df3).
- A digit run keeps spaces and class-3 characters, so "1990 12:30 50%" reaches the number handler as one string.
- After the word "puhelin", or for a number with a leading zero, digits are spoken one by one.
- With one 126-character word and no space, the original does not finish in the harness: it stays in the screen-wrapping loop of 0x1099c (pc 0x109f1). Cause not established (could be the harness's 80-column screen model). The port has no such loop and speaks the word.
- Inline options work through the same parser as the command line, at any position of the text, including inside a word: `-sop` in the middle of a sentence would switch to Sophia.

Inferred:

- The port is equivalent to the original except for the content of uninitialised stack bytes. Basis: the stage calls match in every case that ran, and the word stage matches byte for byte in every case when it is given the original's stack bytes. Where the stale bytes are pointers and return addresses they depended, on a real PC, on the DOS version and memory layout, so the original was not reproducible there either.

Not done: reproducing those pointer bytes (it would mean modelling segment values and heap addresses of one particular DOS environment); the full-screen interface; saving settings back to a voice file; a library API beyond what `puhu` needs.

Decisions taken (the owner can overrule): the port keeps the original's two off-by-one bugs and its stale-stack reads rather than "fixing" them, because any fix changes the stream and the reference could no longer prove the rest; pauses are rendered with the PC's timer timing (n × 64 ticks) unless `--nominal-pauses`; fractions of a sample are carried from pause to pause.

### Documentation pass (same day)

Written for reading rather than as a log: `docs/how-it-works.md`, `docs/voice-format.md`, `docs/original-program.md`, `docs/verification.md`, `docs/method.md`, `port/README.md`, `tools/README.md`; the target `README.md` is now the index. Statements in them were taken from the C source and the saved evidence, and the examples were run (the C interface example compiles and speaks; all relative links resolve).

New tool and evidence: `tools/smpdump.py`, output for the three voices in `evidence/smpdump-*.txt`.

Observed with it:

- Block 1 parses as exactly 149 entries in 1825 bytes; block 2's three tables each end exactly where the next begins (7 units / 39 variants, 3 / 3, 14 / 22). Table 4 is absent in all three files.
- The exception list, the three contour templates and block 0 from 0x400 on are byte-identical in the three voices. The voices differ in the parameter words and in block 2.
- Block 0 offsets 0x40–0xFF are zero. Offsets 0x400–0xBB7 (1976 bytes) are not zero and are not read: the block 0 pointer (DS:0x1b09, copied to DS:0x1d82) is used only in 0x126eb, 0x13543, 0x14813 and 0x14dc9, and none of them reaches past 0x3FF. The data looks like further curves in 256-byte steps (unsigned, rising from about 80 to 176 and falling).
- Coverage: 37,509 of 39,489 bytes (Janne), 37,564 of 39,540 (Kristian), 39,590 of 41,566 (Sophia). The remainder is that unread range, plus `0D 0A 0D 0A` at the end of JANNE.SMP.
- The exception list contains entries for characters of class 4 (`"`, `@`, `\`, `±` …), which `char_class` drops before any lookup, and two keys twice (`atk`, `mm`); the second of each can never match.

Unknown: what the unread curves were for.

## 2026-10-09 (fourth session) — virtual PC speaker, ALSA, piping

The owner asked for a PC-speaker output device with ALSA playback, device selection by option, and piping. Machine: ai. `libasound` 1.2.16.1 with headers is installed; the kernel (7.1.5-arch1-2) has `pcspkr` but not `snd-pcsp`.

Written: `port/src/pcspk.c` + `pcspk.h` (pulse-width table, pulse train, rendering), `port/src/out.c` + `out.h` (the two output devices, WAV/MP3, ALSA, external players; moved out of `puhu.c`), `tools/pcspk_oracle.py`, `port/tests/check_pcspk.py`, `port/tests/pcspk_dump.c`, `docs/pc-speaker.md`. New in `puhu`: `-d/--device dac|pcspeaker`, `--machine`, `--timing`, `--speaker`, `--rate`, `-D/--alsa-device`, `--list-devices`, line-by-line speaking of piped text, `make install`.

| Command | Result |
|---|---|
| `$P tools/pcspk_oracle.py 400 2500` | the original's table builder and player run standalone in Unicorn; table[0] = 109, sample 128 → 52 passes on, 57 off |
| `make -C port check-pcspk` | table and passes per sample: 12 calibrations, 0 differ from the original. Steady levels: 70 checks, largest duty error 0.00000, largest ripple 33 of 16384. Pause lengths correct. |
| `make -C port check` | PC speaker as above; word stage 68 of 68; end to end 90 cases, 3 failed, 1 skipped (unchanged) |
| `puhu -D null`, three piped lines | exit 0 |
| `puhu -D "file:/…/x.raw,raw"` for `dac` and for `pcspeaker`, compared with the payload of `-o x.wav` | identical bytes in both cases |
| `puhu -D hw:9,9 x` | message, exit 1 |
| `echo testi \| puhu` on ai over SSH | exit 1: ALSA `default` cannot be opened and `paplay` has no server |
| `build/puhu-asan` on 6 PC-speaker option sets and on piped input | no reports |
| `puhu` against `puhu-emu`, DAC device, default and `--nominal-pauses` | identical WAV |
| `make -C port install` | `~/.local/bin/puhu` → `port/build/puhu`; `puhu` then works from `/tmp` and finds the voices |

Not run: any playback on a sound card. `/dev/snd/*` is `root:audio` and this user is not in `audio`; `snd_device_name_hint` returns only `null`.

Observed (code):

- 0x152b7 for s = 0..128: v = s·22727 / (64 + s/2); on-time 22727 − v goes to table[0x81 − s], 22727 + v to table[0x81 + s]; each as `round((ns − overhead) / loop) + 1` passes, or 1 when `loop/2 + overhead > ns`; finally table[0] = table[0x100] + 1. The division helper 0x1889b returns quotient and remainder; the remainder is doubled and compared with the loop time for the rounding.
- The player uses table[b + 1] passes with the speaker on and table[0] − table[b + 1] with it off (0x15656–0x15667).
- The calibration loop (0x1585c–0x1587b) has 17 instructions; the real player's loop (0x15643–0x1566a) has three more: `inc di`, `cmp byte [0x1db4],0`, `jne`. Six instructions lie between the two port writes.
- At a pause record the player switches the speaker bit off before returning (0x156d9–0x156db).

Observed (model output, from the ported table):

| Machine preset | Pulse rate | Different widths | Sample values at the shortest pulse | Duty at 128 |
|---|---|---|---|---|
| ideal (1, 0) | 22,000 Hz | 255 | 1 | 50.0% |
| 486 (100, 800) | 21,930 | 204 | 1–9 | 48.6% |
| 386 (400, 2500) | 21,930 | 108 | 1–27 | 46.4% |
| at (1100, 7400) | 21,142 | 36 | 1–66 | 37.3% |
| xt (3600, 33000) | 20,080 | 4 | 1–128 | 19.9% |

Inferred:

- The calibration values of the presets. Derived on paper from published instruction timings (8088: `LOOP` taken 17 cycles, the calibrated loop about 150–165 cycles with prefetch; 286: about 9 and 59 cycles; 386: about 13 and 83; 486: about 6 and 50). Not measured.
- The 30/70 split of the overhead around the "speaker off" write, from the timings of the six instructions between the port writes.
- On slow machines the table makes every pulse too short by the part of the overhead that lies outside the on-time, because 0x15333 subtracts the whole overhead from the on-time. If the XT estimate is near the truth, only positive peaks modulated the speaker there, which would explain why the voices are saved at volume 8 (a clipped signal survives a four-level output).
- The loudspeaker filter (high-pass 300 Hz, +6 dB at 2 kHz, low-pass 6 kHz) is invented.

Unknown:

- How any of this compares with a real PC. No recording of MikroPuhe on hardware is at hand.
- Whether `puhu` plays correctly on a real ALSA card and through `snd-pcsp`.

Decisions taken (the owner can overrule): the default machine is `386`; the default loudspeaker is `cone`; `--pc-speaker` is kept as an old name for `--device pcspeaker --machine ideal --speaker none`, which is not the same output as before (it was the duty curve per sample at 22 kHz; `puhu-emu --pc-speaker` still is); piped text is spoken line by line only when it is played, not with `-o`.

### Later the same session — playing from an SSH session

The owner ran `echo "Hei Michael, tämä on KITT" | puhu -v sophia` in the VS Code terminal on ai and got only `paplay`'s "Connection refused". Cause as noted above: no access to `/dev/snd`, no PulseAudio or PipeWire-Pulse server (`pipewire` runs, but `pipewire-pulse`, `pipewire-alsa` and `wireplumber` are not installed). The owner connects from the Mac.

Changed in `port/src/out.c` and `puhu.c`:

- External players are tried in turn and quietly; a failure is reported by `puhu` in its own words. `ffplay` is skipped when neither `/dev/snd` nor a Pulse server is reachable, because it exits 0 even when it cannot open a sound device (observed).
- Last resort inside a VS Code remote terminal (`VSCODE_IPC_HOOK_CLI` set, `code` on the PATH): the recording is written to `/tmp/puhu-<uid>.wav` and opened with `code`.
- `PUHU_PLAY_CMD`: a user command that receives the WAV on standard input, tried first.
- `--why-silent` prints the situation on the machine and the ways out.
- With standard output redirected to a regular file or a pipe, and no `-o`, the WAV goes to standard output.

| Command | Result |
|---|---|
| `echo "Hei Michael, tämä on KITT" \| puhu -v sophia -s 4 -V 4` from this session | recording opened in the owner's VS Code; the owner: "I heard it" |
| the same with `VSCODE_IPC_HOOK_CLI` unset | clear message, exit 1 |
| two piped lines with `PUHU_PLAY_CMD='nc -N 127.0.0.1 47140'` and `nc -l` on that port standing in for the Mac | two WAV files received; the first byte-identical to `puhu -o` for that line |
| `echo "Hei Michael, tämä on KITT" \| puhu -v sophia > sophia.wav` | 3.16 s WAV, identical to `-o sophia.wav` |
| `make -C port check` | unchanged |

Not run: anything on the Mac (the zsh function, the `nc -l` loop with `afplay`, the `RemoteForward`). They are documented in `docs/pc-speaker.md` as untested.

Documentation: the playing, piping and SSH material now lives in `docs/playback.md` (moved out of `docs/pc-speaker.md`, which keeps the model). Mentions of `docs/pc-speaker.md` for those topics in the entries above mean that file.

While testing the examples for `docs/playback.md`: the redirect rule also fired for `puhu -D null` when standard output was a pipe, and the WAV went to the pipe instead of the device. Changed: `-D` and the new `--play` take precedence over the redirect rule. Checked: `-D null` with a piped standard output writes 0 bytes there; `--play` with `PUHU_PLAY_CMD` likewise, and the command receives the WAV; a bare `echo moi | puhu | wc -c` gives 9,439 bytes of WAV; `puhu "hyvää päivää" | sox -t wav - -r 48000 out.flac` gives a 48 kHz file.

### Listening check of the virtual PC speaker (the owner, 2026-10-09)

The owner listened to recordings from `puhu -d pcspeaker` with the machines `386`, `486` and `ideal`, each with `--speaker cone` and `--speaker none`. His verdict: it sounds right, and with the cone filter "just like back in 1995".

What this is: one listener's recollection of how the program sounded on a PC, about thirty years on. It supports the loudspeaker filter and the fast-machine presets as plausible. It is not a comparison with a recording or with hardware, and it says nothing about the `at` and `xt` presets, which he did not mention.

### Version 1.0 (2026-10-09)

The owner asked for the state at this point to be committed as version 1. `puhu --version` prints `puhu 1.0`; the commit is tagged `mikropuhe-puhu-v1.0` in the project repository. It contains: the native port with both output devices, ALSA playback, piping and the remote fallbacks, the harness and tools, the evidence, and the documentation. Checks at the tag: `check-pcspk` passes, `check-phonemes` 68 of 68, `check-e2e` 86 of 89 streams identical (the three known cases).

## 2026-10-09 (fifth session) — voice tool, and the full-screen interface

After version 1.0 was tagged, the owner asked for work on the voice files and for a replica of the program the original started without arguments.

Written: `tools/smptool.py`, `tools/ui_explore.py`, `tools/gen_ui_tables.py`, `port/src/ui.c` + `ui.h` + `ui_tables.h`, `port/tests/check_ui.py`, `docs/interface.md`; `puhu -v PATH`, `--ui`, `--lines`, `--ui-script`; harness: BIOS scroll/clear (int 10h AH=06/07), read and write character at cursor, `Dos.screen_cells()`, `Dos.typed_late`. Evidence: `evidence/disasm-ui-112c6.txt`, `disasm-ui-11713.txt`, `disasm-ui-1606a.txt`.

| Command | Result |
|---|---|
| `tools/smptool.py roundtrip` on the three voices | identical file each time (39489, 39540, 41566 bytes) |
| unpack Janne, change name / speed 4 / volume 4, add five exceptions, pack; `tools/smpdump.py` on the result | 154 entries, all bytes accounted for as before |
| the original in the harness with that file mapped as `JANNE.SMP`, and `puhu -v` with the same file | the original says `vai_fai … guugl … ii_meil … kit`, takes settings 4,4,0,4,1,1; `puhu`'s stream identical (126,462 samples) |
| `tools/ui_explore.py` with no keys | the start screen; said: the greeting; stops waiting for a key at 0x19479 |
| `tools/ui_explore.py F2 F2` | the error box "Tiedosto oli luku- tai kirjoitussuojattu." (the harness refuses file writes) |
| `port/tests/check_ui.py` | 26 key sequences, 0 differ from the original, on the first run of the finished port |
| the same with one key withheld from the port | reported as a difference (row 9, and the missing spoken word) |
| `check_ui.py --random 60 --seed 7`, then 150 each with seeds 1, 2, 3 | 510 sequences, 0 differ |
| `make -C port check` | PC speaker passes; word stage 68 of 68; interface 126 sequences, 0 differ; end to end 3 known failures |
| 66 interface scripts through `build/puhu-asan` | no reports |
| `puhu` under a pseudo-terminal: start, type, F2, a change, Enter, Esc | alternate screen entered and left, texts present, exit status 0, the "could not be played" note printed after leaving |
| save from the interface into a writable copy of JANNE.SMP after changing speed and volume | "talletettu"; 2 bytes of the file differ; `smpdump` reads it |

Observed (original, from code and from the harness):

- With no arguments `main` calls 0x11c57. It allocates 0xA00 bytes for ten history lines of 256, sets colours by video mode (0x11ada), draws the title and key bar (0x11bd5), speaks the greeting at DS:0x07ff, empties the keyboard buffer, and loops on keys. Enter on a non-empty line stores and speaks it through 0x11fbf, i.e. `lib_speak` directly: no sentence cutting, no inline options.
- The input box is a general text-field routine in segment 1594 with a descriptor of 0x23 bytes (label, buffer, allowed characters, maximum length, position, frame flag, four attributes, and the computed first column, width, scroll offset and cursor). Its key table at 0x16098 has ten keys: Backspace, Enter, 0x7F, Home, Left, Right, End, Del, Ctrl-Left (0x173), Ctrl-Right (0x174). Other characters from 0x20 to 0xFF are inserted.
- The field draws its fill with attribute +0x19 (0x1F) and its text with +0x18 (0x0F).
- The settings screen is driven by 12 rows of 11 bytes at DS:0x0292 (label pointer, spoken-name pointer, type, offset into the settings). Types: 1 switch, 2 number, 3 empty row, 4 the letter c, 5 the voice.
- Settings keys (table at 0x11799): Enter, Esc, Space, `+`, `-`, F1, F2, Up, Down. Before acting, the loop at 0x11762 reads keys for as long as one is pending, so the last one typed counts.
- F2 in settings calls 0x11563, which opens the voice file for writing and writes 0x2E bytes of DS:0x1a98 after the name.
- The serial number: 0x124ea takes the address of the NUL that ends the string at DS:0x096d and returns the inverted 32-bit value after it.
- The exception lookup is given the word with its punctuation: `google,` did not match the key `google` (traced words above).
- In the interface, settings word 13 is 1 (as loaded from the voice), so sentence-end pauses are spoken there; the command-line path clears it.

Inferred: a key press stops speech in the original (the int 9 hook at 0x14ed5 sets the abort flag that `lib_speak` checks). Not exercised: the harness delivers no hardware interrupts.

Unknown: how the port's interface feels at a real terminal; nobody has used it by hand yet.

Decisions taken (the owner can overrule): `puhu` with no text at a terminal now opens the interface (before, it spoke typed lines; that is `--lines` now); speech in the interface cannot be interrupted; the banner shows the original's serial number 111; a failed playback does not disturb the screen, a note is printed on leaving.

Future idea noted in `port/PLAN.md`: an English pronouncing mode and English voice packs.

### Later: "F1 Apu is not working yet" (the owner, in the VS Code terminal on ai)

Two causes, both real there. VS Code keeps F1 for its command palette, so the key never reaches a program in its terminal. And the help is speech only: on ai, where nothing can be played, a working F1 would also have shown nothing.

Changed in `port/src/ui.c` (terminal backend only): Ctrl-A and Alt-1 act as F1, Ctrl-P and Alt-2 as F2; when the host reports that speech could not be played, the text is shown on rows 21–23; a grey reminder of the extra keys on row 24. `mp_ui_host.speak` now returns whether the speech was audible.

Checked through a pseudo-terminal: without a player, the greeting, the main help (by Ctrl-A, by Alt-1 and by the `ESC O P` form of F1) and the settings help appear as text, Ctrl-P opens the settings; with `PUHU_PLAY_CMD='cat > /dev/null'` standing in for a working player no captions appear. `check_ui.py`: 26 sequences, 0 differ (the additions are not in the compared screen buffer).

Not checked: the keys in the actual VS Code terminal. Whether VS Code passes Ctrl-P through depends on the platform (on macOS its own shortcut is Cmd-P).

The owner, after the change: "Now it works". So the interface has been used by hand in the VS Code terminal over SSH (silent, with the captions), and the help is reachable there. Which of the extra keys he pressed was not said. Still open: the interface with real sound.

### Later: sound from ai on the Mac, step by step

The owner asked for step-by-step instructions for the tunnel. Thinking it through showed that the `nc -l … && afplay` loop I had documented was not good enough for the interface: `nc` returns as soon as the data is sent, so `puhu` would carry on while the Mac is still playing, the next sentence would find no listener, and the interface stops trying after one failure.

Written instead: `port/puhu-listen` (Python 3, standard library only). It listens on 127.0.0.1, plays each received WAV, and closes the connection only when the player has finished; `nc -N` on the sending side waits for that. With `--tunnel HOST` it also runs `ssh -N -R PORT:127.0.0.1:PORT HOST`.

Checked on ai with a stand-in player (keeps the file, sleeps one second): one sentence returns after 1.0 s, three piped lines after 3.0 s, the file equals `puhu -o`; the interface under a pseudo-terminal spoke nine recordings through it and showed no captions; 7 bytes of non-WAV are ignored; `--tunnel invalid.invalid` ends with the "tunnel ended" message.

Not run: the Mac side (`afplay`, the real tunnel). `docs/playback.md` has the steps and a table of what to check when nothing is heard.

### The tunnel works (the owner, 2026-10-09, about 21:30)

The owner set up way 3 and reported "WOW it works", and that the full-screen `puhu` works too. So `puhu` on ai is now heard on the Mac: single sentences and the interface.

On the way there the listener was started on ai twice instead of on the Mac. First `python3 ~/puhu-listen --tunnel ai` in a terminal on ai, where the file did not exist. Then, after copying the file within ai, the same command again: it ran, opened a tunnel from ai to ai, received a 3.2 s recording and failed to play it with `paplay` ("Connection refused", "the player exited with status 1"). Found by looking at ai: `pgrep` showed `/usr/bin/python3 /home/user/puhu-listen --tunnel ai`, and `ss -ltnp` showed that process on port 4714.

Observed on ai after it worked: port 4714 is listening with no process of the owner's attached (the SSH server holds it for the forward from the Mac), there is no `puhu-listen` process on ai, and `~/.zshrc` line 189 has `export PUHU_PLAY_CMD='nc -N 127.0.0.1 4714'` (added by the owner).

Changes that came out of this: `puhu-listen` warns when started inside an SSH session; `docs/playback.md` has an overview table of the three ways, a "day to day" paragraph, the mistake and how to recognise it, and a troubleshooting row for it.

The owner on the sound over the tunnel: "voice is sooooo clean, like in perfect PC speaker". Which output device and settings he was listening to was not said (the default is the DAC device at the voice's saved volume 8). Not known: which player line the Mac's listener printed (expected `afplay`).

Still untried: way 2's zsh function; ALSA on a real card; `snd-pcsp`.

### Saving settings (the owner: "can't save default setting")

F2 in the settings screen gave the original's "Tiedosto oli luku- tai kirjoitussuojattu." box, because the voices in `original/files/` are read-only and the original saves into the voice file. Changed in `port/src/puhu.c`: when a shipped voice cannot be opened for writing, it is copied to `$XDG_DATA_HOME/mikropuhe/` (default `~/.local/share/mikropuhe/`) and the settings are written into the copy; a voice found there is loaded in preference to the shipped one. `--voices-dir` switches both off.

Checked with `XDG_DATA_HOME` pointing at a scratch directory: speed and volume set to 4 and saved from the interface script: "talletettu", no error box, a 39,489-byte `JANNE.SMP` appears there with settings 4,4,0,4; the next `puhu` run starts with those settings; Sophia still starts with the shipped 0,0,0,8; `--voices-dir original/files` gives Janne's shipped 0,4,0,8; originals 5 of 5; `check_ui.py` 26 sequences, 0 differ (the checks pass `--voices-dir`).

Not checked: the owner's own run of it.

## 2026-10-09 (sixth session) — macOS

The owner asked to plan the C program for macOS, then pointed out that ai can reach his Mac: `ssh` to its address (macOS 12.7, x86_64; key login works). The note in CLAUDE.md that ai cannot reach the Mac no longer holds.

Found on the Mac: Apple clang 14.0.0, GNU Make 3.81, Python 3.9.6, `afplay`, `afconvert`, `nc`; no lame, ffmpeg, sox, pkg-config, brew. 33 GiB free. The project clone `~/Projects/rea/project` is at 03bb654 (the first two commits), remote `ai`. `~/bin` and `~/.local/bin` are on the interactive PATH and writable. `puhu-listen --tunnel ai` was running.

Done from ai over SSH, in `/tmp/puhu-mac` on the Mac (sources copied with tar, nothing installed):

| Step | Result |
|---|---|
| `make` in `port/` | built first time, 0 warnings |
| 156 cases (90 `--events`, 66 `--ui-script`) on both builds | first run: 154 identical; the two "save fails" interface scripts differed. Cause: in the Mac's clone the voice files are writable, so F2 saved into `original/files/JANNE.SMP` instead of failing (same bytes as before, checksum still OK, date changed to 21:46). |
| after making saving never write into the voices directory | 156 of 156 identical; originals 5 of 5 on the Mac |
| one sentence to WAV on both, DAC and `-d pcspeaker --machine at` | byte-identical files |
| started through a link from `/`, no `--voices-dir` | voices found |
| `--play` of one sentence | `afplay` exit 0, 4 s. First attempt without `--play` wrote the WAV to the SSH pipe (the redirect rule), i.e. into my own output. |
| `-o x.m4a` | AAC, 22,050 Hz, 3.2 s (`afinfo`); `-o x.mp3` refused with the hint |
| interface in a pseudo-terminal on the Mac | start, typing, Ctrl-P, Enter, Esc all as on Linux |
| F2 save with `XDG_DATA_HOME` in the scratch directory | copy of JANNE.SMP created there |

Source changes: `program_dir()` with a macOS branch (`_NSGetExecutablePath`), `mkstemps` with a `.wav` ending for players that want a file, macOS text in `--why-silent`, combining diaeresis and ring composed in `utf8_to_cp437`, `.m4a` output, and the save rule above. Before the Mac was reachable the macOS branch had been compiled on ai against a stand-in header; on the Mac it then worked unchanged.

Not known: whether the sentence played at 21:5x was heard. Not done: installing on the Mac (needs a commit on ai and a pull there), the interface by hand in Terminal.app, `make check` on the Mac, Core Audio output.

Also noted in `port/PLAN.md` at the owner's request: the English mode is to be selected with `--language fi|en`, short `-l`.

### Version 1.1 (2026-10-09)

The owner agreed to commit, and to have the Mac's clone pulled and installed. Before that he had pulled and built on the Mac himself and got `puhu: /../../original/files/JANNE.SMP: cannot open`: that was the 1.0 commit, which finds its voices through `/proc/self/exe` and so cannot work on macOS. `puhu --version` now says 1.1, to tell the builds apart.

Merged as 329e397 and tagged `mikropuhe-puhu-v1.1` on ai. On the Mac (over SSH): `git pull --ff-only ai main` in `~/Projects/rea/project` (95def56 → 329e397), `make -C mikropuhe-1.0/port install`; `puhu --version` there says 1.1, the voices are found through the link, and the 156 comparison cases give the same combined hash from the installed Mac build as from the ai build. The scratch directory `/tmp/puhu-mac` was removed. Originals on the Mac: 5 of 5.

The owner, after running the installed program on the Mac: "Works just like in 1995 also on modern 9500F processor, in a Hackintosh". So the Mac is a Hackintosh (it reports as an Apple machine, macOS 12.7, x86_64). What exactly he ran there, the command line or also the full-screen interface, was not said.

The owner sent a screenshot of the full-screen interface running natively in Terminal.app on the Mac (window 226×46, title "puhu — afplay"): the 80×25 screen is drawn in the top left corner with the right frame characters and colours; his words: "Not full in console, but cool in left corner." Not shown or mentioned: the settings screen and the function keys there.

A second screenshot from the Mac shows the settings screen in Terminal.app: all rows, the highlighted row, the settings key bar and the key reminder, with values that had been changed (speed 2, pitch variation 3, volume 5). The owner: "Looks perfect".

The owner: "and settings were saved, too". Looked at on the Mac over SSH: `~/.local/share/mikropuhe/JANNE.SMP` exists (39,489 bytes) and `smpdump` reads its saved settings as speed 2, pitch 4, pitch variation 3, volume 5; the originals in the clone still pass 5 of 5, and `original/files/JANNE.SMP` keeps its earlier date. So saving into the user's copy works on macOS as designed.

Listening, the owner with friends (2026-10-09, on the Mac): the voice at its saved settings is "raspy and full of soul"; `puhu -d pcspeaker --machine xt` "sounds like it is under a pile of snow". Both fit the numbers (volume 8 clips about 40% of the samples; the `xt` preset leaves a duty swing of 20–42% in four steps, RMS about a quarter of the `386` preset's), but neither is a comparison with real hardware.

After the suggestion to try `--machine at` and `--machine xt --speaker none`: "yeah, louder and clearer now". Which of the two, or both, was not said.

The owner: "macOS version is also perfect." He then asked about DOS (DR-DOS) and Windows 11. Looked at for the plan: on ai none of MinGW-w64, Wine, DJGPP, Open Watcom, DOSBox is installed, `qemu-system-i386` is; `synth.c`, `text.c`, `voice.c` contain no POSIX call (`grep` for termios, select, popen, mkstemps, readlink, realpath, getline, isatty, unlink, mkdir, access, system, strcasecmp, ALSA, getenv: 0 lines), `out.c` 26, `puhu.c` 21, `ui.c` 9, `pcspk.c` 1. The plan is in `port/PLAN.md` under "Ideas for later". Inferred there, not tested: that the original's delay-loop calibration breaks on processors where one loop pass takes less than a nanosecond.

### Windows build (2026-10-09, evening)

The owner installed the cross-compiler ("Installed mingw64"): `x86_64-w64-mingw32-gcc (GCC) 16.2.0`, mingw-w64 headers version 14, C runtime UCRT (`_UCRT` defined). Wine is not installed on ai or on the Mac.

**Observed**

- `-fsyntax-only` per file before any change: `voice.c`, `synth.c`, `text.c` no message; `pcspk.c` `M_PI` undeclared; `out.c` `mkstemps`, `getuid`; `puhu.c` two-argument `mkdir`, `realpath`, `getline`; `ui.c` no `sys/ioctl.h`.
- After the Windows branches were written: all files compile with `-Wall -Wextra -Wconversion` without a message. `puhu.exe`: PE32+ console program, 87,552 bytes stripped, imports `KERNEL32`, `SHELL32`, `WINMM` and the `api-ms-win-crt-*` set (`evidence/windows/puhu-exe-imports.txt`).
- Docker is usable from the owner's account on ai and `ubuntu:24.04` was already there. Image `puhu-wine` built from it with the `wine64` package (Wine 9.0, `9.0~repack-4build3`), 1.57 GB with its prepared Wine folder. Containers run with `--network none` and read-only mounts.
- `make -C port check-windows` (`evidence/windows/check-windows.txt`): 156 of 156 cases identical to the Linux build; six sound files identical (`edges-windows.txt`, `edges-linux.txt`), among them both virtual PC speaker files; code page 850 pipe and code page 1252 file give the same sound as UTF-8; F2 F2 by key script creates `drive_c/users/root/AppData/Roaming/mikropuhe/JANNE.SMP`; in Wine's console through a pseudo-terminal the main screen is drawn, letters arrive, F2 and Ctrl-P open the settings, `+` changed Korkeus from 4 to 5 (first manual run), Esc ends the program.
- `--play` under Wine: `PlaySound` fails (ALSA messages from Wine: no card), `puhu` prints its message and exits 1.
- A first Ctrl-P test showed the settings screen only after the next key. Cause: the Docker client holds Ctrl-P back (start of its detach sequence). With `--detach-keys ctrl-^` Ctrl-P arrives at once.
- The same changed sources on the Mac (scratch directory `/tmp/puhu-mac`, removed afterwards): build without messages, `puhu --version` 1.2, 156 of 156, the six sound files identical to Linux (`evidence/windows/edges-macos.txt`), originals in the clone 5 of 5.
- Linux after the changes: `make check` as before (same three known stream differences), builds with and without ALSA without messages, sanitizer build clean on a speech input and on three piped lines (the new `read_line`).
- My mistakes on the way, both cleaned up: a wrong path in the first `check.sh` made Docker create empty root-owned folders `port/tests/build/mikropuhe-windows` and `port/tests/tests` (removed with `rmdir` through a container); a zsh variable used as a command line made the Linux side of the first sound-file comparison print usage text (redone with a shell function).
- Five untracked sound files that were in `mikropuhe-1.0/` at the start of the evening were gone when I next looked. No command of mine names them or that folder's files.

**Inferred**

- That the build behaves on Windows as under Wine for everything that does not touch the console or the sound device: same compiler output, and those paths use the C runtime and a few `KERNEL32` calls that Wine implements.
- That the manifest (`activeCodePage` UTF-8) makes file names with ä and ö work on Windows 10 1903 and later. Under Wine the file name case passes, but Wine's own locale was UTF-8 there, so this run does not show it.
- The PowerShell remarks in `docs/windows.md` (`>` rewrites program output before 7.4; pipes use `$OutputEncoding`) are from what is known about PowerShell, not from a run.

**Unknown**

- Whether `PlaySound` plays the in-memory WAV on Windows 11. It has never made a sound.
- How the interface looks and which keys arrive in Windows Terminal, in a PowerShell window and in `cmd`.
- Whether Windows asks before starting the unsigned program.
- Which Windows machine the owner will try it on.

New in the tree: `port/win/` (manifest), `port/tests/portable/` (cases, `run.sh`, `edges.sh`, `expected.txt`), `port/tests/wine/` (Dockerfile, `check.sh`, `ui_smoke.py`), Makefile targets `windows`, `fingerprint`, `check-windows`, `docs/windows.md`, `docs/porting.md` (the owner: "please document these cool work methods for us"). `puhu --version` says 1.2. Also for every system: voices lying next to the program are used; `read_line` replaces `getline`.

### On Windows 11 itself (2026-10-11)

The owner: "I have Windows 11 running as VM on the Unraid machine now", then: "My Windows 11 has SSH enable, so you can try finding it an copying the files to it".

**Observed**

- Reading the SSH greeting of every address in the home network from ai: one says `OpenSSH_for_Windows_9.5`, with a virtual machine's network address. ai's key is refused there (`Permission denied (publickey,password,keyboard-interactive)`). The Mac can log in there: `whoami` gives the expected user. All work went ai → Mac → Windows.
- The machine: Windows 11 Pro, version 10.0.26300.9457, model "Standard PC (i440FX + PIIX, 1996)", default shell `cmd`, code page 850, PowerShell 5.1, `tar` present, no `sh`. Sound devices listed: NVIDIA High Definition Audio, Steam Streaming Speakers and Microphone, NVIDIA Virtual Audio Device. `query user`: nobody logged in at the screen.
- Copied `port/build/mikropuhe-windows` to `C:\Users\NAME\mikropuhe-windows` as a tar stream; SHA-256 of all four files equal on both sides (`puhu.exe` 7d624937…).
- `run.ps1` there: 156 lines, `diff` against `expected.txt` empty (`evidence/windows/win11-cases.txt`).
- `edges.cmd` in a `cmd` session: six hashes, each equal to the Linux build's for the same case, among them `echo Hei Michael, tämä on KITT| puhu` (code page 850 through a real `cmd` pipe) and the file named `ääkköset.wav` (`evidence/windows/win11-edges.txt`).
- `puhu --play -s 4 -V 4 Hei Michael, MikroPuhe toimii nyt Windowsissa`: exit 0 after 3.5 s; the sentence is 3.4 s long.
- `--ui-script "0x13c 0x13c"`: `%APPDATA%\mikropuhe\JANNE.SMP` appears, 39,489 bytes; the shipped `JANNE.SMP` keeps its hash.
- The interface through `ssh -tt` twice over and a pseudo-terminal on ai (`evidence/windows/win11-interface.txt`). Run 1: main screen, frame characters, key bar, F2, `+` (Korkeus 5), Ctrl-P, Esc all as expected; letters typed 16 s after the start did not appear. Run 2, typing 28 s after the start: `pää` appears, Backspace leaves `pä`, Left Del `x` gives `px`, Esc ends it. Each run played the greeting on that machine.
- Removed from the Windows machine afterwards: my folder `C:\Users\NAME\mikropuhe-test` and the test copy `%APPDATA%\mikropuhe\JANNE.SMP` with its folder. Left there: `C:\Users\NAME\mikropuhe-windows`.

**Inferred**

- Run 1's missing letters: the greeting was still being played (on Windows it really takes its time; under Wine playing failed at once), and the interface discards keys typed while it speaks, as the original does. Run 2 fits; the length of the greeting was not measured.
- That the sentence was sent to a sound device: the call lasted as long as the sound and reported success.

**Unknown**

- Whether anything was audible, and on which device.
- How the interface looks in Windows Terminal or a `cmd` window, and whether F1, F2 and AltGr characters of a real keyboard arrive.
- What SmartScreen does when the program is started from Explorer.

New in the tree: `port/tests/portable/run.ps1` and `edges.cmd` (the latter stored in code page 850), `evidence/windows/win11-*.txt`.

The owner, after using the program at the Windows 11 machine (2026-10-11): "Had so much fun, it worked perfectly!" Not said: what he ran (the command line, the full-screen interface, which keys), in which terminal, and on which sound device he heard it.

The owner, same day: "I even tested in console, the plain one, and the UI looked great". So the full-screen interface has been seen in the classic console window on Windows 11 (as opposed to Windows Terminal). Not said: a screenshot, which keys, the settings screen.

### Kone, a voice of our own (2026-10-11)

The owner asked: "can we create a custom voice perhaps?" and chose a synthetic one first.

**Observed: Kone**

- `tools/make_synth_voice.py` writes a voice directory; `smptool.py pack` makes `voices/KONE.SMP`, 34,678 bytes; `roundtrip` identical; `smpdump` explains every byte; 92 exceptions, 64 unit files. Generating it twice gives the same bytes.
- The original in the emulator with this file in place of its voices: six sentences (numbers, doubled stops, exceptions, signs) give WAV files identical to `puhu -v kone`.
- Measured resonances of the eight vowel periods (strongest three harmonics-envelope peaks below 3.6 kHz): a 660/1100/2530, e 440/1980/2530, i 220/2310/2970, o 550/880/2420, u 330/660/2420, y 330/1760/2310, ä 660/1650/2530, ö 440/1540/2310 Hz: each within one harmonic of what the script asks for. The recorded Janne periods measured the same way agree for e, i, y, ö; for a, o, u the measure picks low harmonics instead.
- `-v NAME` now finds `NAME.SMP` (own voices, voices directory, `voices/`); the three original names and their abbreviations behave as before (156 cases unchanged; `-v k` is Kristian). `make check` as before; `make check-windows` passes with the rebuilt `puhu.exe` (88,576 bytes), and `-v kone` gives the same file under Wine as on Linux.
- `whisper` is on ai's PATH but does not start (module missing), so no machine listening was done.

**Inferred**

- That Kone is intelligible. The vowels should be: their formants are right. The consonants are designs on paper.

**Unknown**

- What Kone sounds like. Nobody has heard it.

Kone on the Mac (2026-10-11, at the owner's request): `voices/KONE.SMP` copied to `~/.local/share/mikropuhe/KONE.SMP` there, same checksum; the installed `puhu` 1.1 speaks it by path with the same sound file as on ai (compared without playing). By name, 1.1 takes `-v kone` for Kristian; that needs 1.2.
