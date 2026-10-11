# How the port is checked against the original

The claim is: `port/build/puhu` produces the same speech as `PUHU.EXE`. This page says how that is tested, what the result is, and where the claim stops.

## The reference: the original, run in an emulator

`tools/puhu_emu.py` loads the unmodified `PUHU.EXE` into a CPU emulator (Unicorn, 16-bit real mode) with a minimal DOS and BIOS around it, and lets it run. Three small patches are applied to the copy in memory, all in the device layer, so that no PC hardware has to be modelled:

| Address | Original | Patched to |
|---|---|---|
| 0x15167 | CPU-speed calibration against the timer chip | return "fast enough" |
| 0x152b7 | build the pulse-width table from the calibration | return at once |
| 0x15616 | the PC-speaker player | empty the ring buffer into a Python list and return |

Everything above the ring buffer is original machine code: option parsing, sentence cutting, exceptions, numbers, unit selection, cross-fade, volume. What the player would have played is recorded as a list of **events**: runs of 8-bit samples and pause counts.

The same input always gives the same events. `tools/make_reference.py` runs 68 cases (10 sentences × 3 voices, and for Janne each setting at its extremes on two sentences) and writes their sample counts and SHA-256 hashes to `evidence/reference/MANIFEST.tsv`. Running it twice gives the same file.

A listening check stands behind the reference: the owner played three recordings made this way on 2026-10-09 and found them good.

## Stage traces

With `Dos.enable_trace()` (or `puhu_emu.py --calls`) the harness also records what crosses the boundaries inside the original:

| Record | Taken at | Content |
|---|---|---|
| `S` | 0x1548a | a speak call begins: ring buffer size, the 14 active settings |
| `G` + `W` | entry of 0x13543 | a word is handed to the word stage: pitch level, "first word" flag, bracket state, volume; the word |
| `F` | entry of 0x13543 | the 0x360 bytes of stack the word stage's frame is about to occupy |
| `R` | exit of 0x13543 | return value, the string left behind, stream position |
| `P` | 0x1551d | a pause requested by the text stage |
| `E` | 0x154e1 | the speak call ends |

The port can write the `S`, `G`, `W`, `P` and `E` records itself (`puhu --trace FILE`).

## The two checks

```bash
make -C port check            # all of them; needs ~/tools/mikropuhe-venv (unicorn)
make -C port check-phonemes
make -C port check-e2e
make -C port check-pcspk      # the virtual PC speaker, see further down
make -C port check-ui         # the full-screen interface, see further down
```

### 1. Word stage in isolation (`tests/check_phonemes.py`)

For every reference case the traced word-stage calls are replayed through the C code (`build/replay`): same word, same globals, same starting stack bytes. The samples and pauses the C code emits for each word must equal the original's, and so must its return value and the string it leaves.

**Result: 68 cases, 0 failed.** This covers `mp_speak_phonemes` and everything below it: unit lookup, durations, pitch, cross-fade, volume, the pause and ring-buffer rules.

### 2. End to end (`tests/check_e2e.py`)

`build/puhu` is run on the same command tail as the original, given as UTF-8. Two things are compared:

- the **stage calls**: the sequence of `S`, `G`, `W`, `P`, `E` records must be identical. This checks the whole text stage: sentence cutting, inline options, character classes, exceptions, number expansion, the state passed to each word.
- the **sample stream**.

The inputs are the 68 reference cases plus 22 extra ones aimed at the text stage (number forms, decimals, "puhelin" digits, units, abbreviations, brackets, every punctuation mark, inline options including voice changes, upper case and umlauts, a 126-character word).

**Result: 90 cases; 1 skipped because the original does not finish in the harness (the 126-character word); of the 89 that ran, stage calls identical in 89, sample stream identical in 86.**

## The three streams that differ

`jan-s07-default`, `kri-s07-default` and `extra-07` differ by 10, 30 and 1 samples in streams of 340,000 to 470,000. The stage calls are identical in all three.

The cause is the original's use of uninitialised stack memory (the two bugs described in [how-it-works.md](how-it-works.md#two-bugs-in-the-original-word-stage)). The port keeps an image of the relevant stack area and places the word stage's frame where the original's was for each caller, so the stale bytes that are the original's *own* earlier data (the previous word's pitch contour, for example) are the same. What it does not have are stale bytes that were never speech data: after a number, the area holds the return address, a far pointer, two saved registers and a local variable of the number handler. Comparing the stack images word by word (`F` records) shows exactly those bytes as the difference.

Two observations support reading this as "equivalent except for garbage":

- with the original's stack bytes supplied, check 1 passes for every word of every case, including these;
- starting the port from a zeroed stack instead of the original's initial stack (`check_e2e.py --no-stack`) changes nothing: the same 2 of 68 reference cases differ.

On a real PC those pointer bytes depended on where DOS loaded the program and on its heap, so the original's output for such words was not fixed either.

## Other checks

| Check | Result |
|---|---|
| `puhu` and `puhu-emu` on the same sentence and options, DAC device, with and without `--nominal-pauses` | identical WAV files (sentence without digits) |
| 10 awkward inputs (very long words, long vowel runs, piles of punctuation, 16-digit numbers, rapid voice and setting changes) through a build with AddressSanitizer and UBSan, `make -C port build/puhu-asan` | no reports |
| The same build on the PC-speaker device (four machines, three rates, custom timing) and on piped input | no reports |
| ALSA playback into ALSA's `file` device, both output devices | ALSA received exactly the bytes `-o` writes as WAV payload |
| `echo … \| puhu -v sophia > sophia.wav` (output redirected, no `-o`) | a WAV identical to `-o sophia.wav` |
| Two piped lines with `PUHU_PLAY_CMD='nc -N 127.0.0.1 PORT'` and `nc -l` on that port | two WAV files received, the first identical to `puhu -o` for that line |
| No usable sound device, inside a VS Code remote terminal | the recording opened in the editor on the Mac and was heard (the owner, 2026-10-09) |
| No usable sound device, outside VS Code | a message naming the cause, exit status 1 |
| `tools/smpdump.py` on the three voices | the format accounts for all bytes the program reads; tables end exactly where the next begins |
| `sha256sum -c original/SHA256SUMS` | originals unchanged |

## The virtual PC speaker (`tests/check_pcspk.py`)

`puhu --device pcspeaker` is described in [pc-speaker.md](pc-speaker.md). Its check has two halves of different weight.

- **Against the original.** `tools/pcspk_oracle.py` executes the original's table builder (0x152b7) and its player (0x15616), unpatched, for given calibration values, and counts the `LOOP` passes between the player's writes to port 0x61 for each of the 255 sample values. The C code must give the same 257 table entries and the same passes. **Result: 12 calibrations (the five machine presets and seven others, up to the edge of "too slow"), 0 differences.**
- **Against its own arithmetic.** There is no original to compare the rendered sound with. The check confirms that the renderer does what the model says: a steady sample value gives a steady level equal to the duty cycle (70 combinations, largest error below 0.00001), the residue of the pulse rate is below −50 dB, a pause lasts *n* × 64 timer ticks, and the number of output samples matches the pulse period.

Not covered by any check: whether the machine presets and the loudspeaker filter resemble real hardware. They are estimates and are labelled so. The only evidence is one listening check from memory: the owner found the `386`, `486` and `ideal` machines with the loudspeaker filter to sound as the program did in the 1990s.

## The full-screen interface (`tests/check_ui.py`)

The interface ([interface.md](interface.md)) is compared with the original's under the same keys. `tools/ui_explore.py` runs the original in the emulator with a list of key presses until it waits for a key that is not there; `puhu --ui-script` does the same with the port. Compared: all 2,000 cells of the 80×25 screen, their colour attributes, the cursor position, whether the program ended, and every word handed to the speech core.

**Result: 26 hand-written key sequences (every screen, every key, history overflow, each kind of setting, a voice change, a failed save, leaving) and more than 600 random ones: 0 differences.** `make -C port check-ui` runs the 26 and 100 random ones.

Two things about the emulator matter here. Keys are delivered only when the original asks for one while none is pending, as when a person types after it has stopped speaking; keys that are "already there" would be discarded by the original's own buffer flush. And the BIOS call the original uses to clear parts of the screen had to be added to the harness.

Not covered: the terminal side of the port (escape sequences, key decoding), which has no counterpart in the original. It was driven through a pseudo-terminal, and the owner used it by hand in the VS Code terminal (2026-10-09): screen and keys worked, including the help through Ctrl-A, and it spoke on his Mac through the tunnel. Saving settings was tried on a writable copy of a voice: two bytes of the file changed, the two settings that had been changed. Interrupting speech with a key is not implemented.

## Voice files written by `tools/smptool.py`

- Unpacking and packing each of the three voices gives back the identical file.
- An edited Janne (new name, other saved settings, five added exceptions) was loaded by the original `PUHU.EXE` in the emulator: it spoke the new exceptions and took the new settings, and `puhu -v` with the same file produced the identical sample stream (126,462 samples).

## The macOS build

The same sources were built on a Mac (macOS 12.7, Apple clang 14). 90 speech inputs with `--events` and 66 interface key scripts with `--ui-script` were run through the Linux and the macOS build: **156 of 156 outputs identical**, and the WAV files of the DAC device and of the virtual PC speaker are byte-identical too. The Mac build therefore stands on the Linux build's comparison with the original. Details and what is untested there: [macos.md](macos.md).

## The Windows build

The same sources, cross-compiled with MinGW-w64, were run under Wine 9.0 in a container on ai (`make -C port check-windows`). The same 156 cases: **156 of 156 outputs identical** to the Linux build. Six sound files made through the system-specific code (file, redirection, pipe, a non-ASCII file name, two virtual PC speaker settings) are byte-identical on Linux, macOS and Windows, which also means the floating-point filter rounds the same on all three. On 2026-10-11 the same program file was run on a Windows 11 machine over SSH: again 156 of 156, the same six sound files, and the interface working in the console of the SSH session. The owner then used it at that machine: "it worked perfectly". Details: [windows.md](windows.md). The method: [porting.md](porting.md).

## Where the claim stops

- **One environment.** The reference is the original in one emulated DOS: DOS 3.30 calls, 640 KB, an 80×25 colour text screen, a 64000-byte ring buffer. Behaviour that depended on the machine is not covered: the timing of the speaker loop, a smaller buffer on a machine with little memory (which would switch the sentence-end pauses back on).
- **The sample stream, not the sound.** The checks above are about the 8-bit stream. The virtual PC speaker has its own check (next section); what a real speaker sounded like is not known.
- **No sound card on ai.** ALSA playback was verified up to the bytes ALSA receives, not on a card: the SSH session on ai cannot open one. What has been heard is `puhu` on ai playing on the Mac through the SSH tunnel and `puhu-listen` (the owner, 2026-10-09), both single sentences and the full-screen interface.
- **Windows, in detail.** The Windows build passes its checks on Windows 11 and the owner reports that it works perfectly there. He saw the interface in the plain console window ("looked great"); single keys and Windows Terminal are not recorded.
- **The shell function of way 2** in [playback.md](playback.md) was not run.
- **The interface's speech interruption** (a key press stopping the speech) is neither tested nor ported.
- **Voice changes inside a text** are covered by one extra case.
- **Inputs.** 89 inputs is a sample. The checks are cheap to extend: add a line to `EXTRA` in `tests/check_e2e.py`, or try one input with `tests/check_e2e.py --tail "/JAN teksti"`.

## If a check fails after a change

1. `tests/check_e2e.py --tail "..."` on the smallest input that shows it.
2. If it reports a **stage call** difference, the text stage is wrong: the message names the first differing word or pause. Compare with `tools/puhu_emu.py --calls -- "..."`.
3. If the stage calls agree and the **stream** differs, run `tests/check_phonemes.py`. If that fails, the word stage is wrong and the message names the word. If it passes, the difference comes from stale stack bytes: the port's stack image no longer matches (look at `mp_frame` and the three `MP_PATH_*` offsets).
